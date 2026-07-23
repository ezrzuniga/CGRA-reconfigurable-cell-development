#include "mesh_wrapper.h"

#include <cstring>

using namespace sc_core;
using namespace tlm;

// Periodo de clk_ (10 ns, ver constructor). Los margenes de esta clase esperan por
// tiempo (multiplos exactos de este periodo), no por clk_.posedge_event(): esperar el
// evento directamente competiria en el mismo delta cycle con los procesos internos
// del mesh que tambien son sensibles a clk.pos() (issue/pc_update/load_program), con
// orden de ejecucion no garantizado entre ambos. Esperar por tiempo es el mismo
// patron ya validado en mesh/CGRA_Mesh_SmokeTest__TB.cpp (advance_cycles()).
static const double CLK_PERIOD_NS = 10.0;

MeshWrapper::MeshWrapper(sc_module_name name)
    : sc_module(name),
      target_socket("target_socket"),
      clk_("clk", 10, SC_NS),
      rst_("rst"), enable_("enable"),
      reset_done_(false),
      mesh_("mesh", std::vector<CellKind>{CellKind::VECTOR}),
      in_N_("in_N"), in_S_("in_S"), in_W_("in_W"), in_E_("in_E"),
      out_N_("out_N"), out_S_("out_S"), out_W_("out_W"), out_E_("out_E"),
      config_(0), start_(0), status_(0), done_(0), programmed_(false)
{
    mesh_.clk(clk_);
    mesh_.rst(rst_);
    mesh_.enable(enable_);
    mesh_.in_N[0](in_N_);   mesh_.out_N[0](out_N_);
    mesh_.in_S[0](in_S_);   mesh_.out_S[0](out_S_);
    mesh_.in_W[0](in_W_);   mesh_.out_W[0](out_W_);
    mesh_.in_E[0](in_E_);   mesh_.out_E[0](out_E_);

    target_socket.register_b_transport(this, &MeshWrapper::b_transport);

    SC_THREAD(reset_thread);
}

// Corre una unica vez al arrancar la simulacion -- no hay transaccion "RESET" en el
// mapa de registros, asi que el reset del mesh es un evento de arranque, no algo
// repetible por diseno.
void MeshWrapper::reset_thread()
{
    // in_N_/in_S_/in_W_/in_E_ no se tocan aqui: ya arrancan en Link() (cero) por
    // default-construction de sc_signal<Link>, y solo handle_input_write() debe
    // escribirlas (un unico "driver" logico -- el hilo que llama a b_transport --
    // evita el error E115 de sc_signal por multiples drivers).
    rst_.write(true);
    enable_.write(false);

    wait(2 * CLK_PERIOD_NS, SC_NS);

    rst_.write(false);
    enable_.write(true);

    reset_done_ = true;
    reset_done_event_.notify();
}

void MeshWrapper::b_transport(tlm_generic_payload& trans, sc_time& delay)
{
    if (!reset_done_) {
        wait(reset_done_event_);
    }

    uint64_t addr = trans.get_address();
    unsigned char* data_ptr = trans.get_data_ptr();
    unsigned int len = trans.get_data_length();
    tlm_command cmd = trans.get_command();

    if (cmd == TLM_WRITE_COMMAND) {
        switch (addr) {
            case 0x00:
                handle_config_write(*reinterpret_cast<uint32_t*>(data_ptr));
                break;
            case 0x04:
                handle_start_write(*reinterpret_cast<uint32_t*>(data_ptr));
                break;
            case 0x10:
                handle_input_write(data_ptr, len);
                break;
            default:
                trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
                return;
        }
    } else if (cmd == TLM_READ_COMMAND) {
        switch (addr) {
            case 0x08:
                *reinterpret_cast<uint32_t*>(data_ptr) = status_;
                break;
            case 0x0C:
                *reinterpret_cast<uint32_t*>(data_ptr) = done_;
                break;
            case 0x14:
                handle_output_read(data_ptr, len);
                break;
            default:
                trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
                return;
        }
    }

    trans.set_response_status(TLM_OK_RESPONSE);
}

// CONFIG (0x00, W) es el punto donde el catalogo de programas se traduce a una
// instruccion real cargada en la unica celda del mesh (row=0, col=0, addr=0).
void MeshWrapper::handle_config_write(uint32_t value)
{
    config_ = value;

    bool ok = false;
    Instr instr = build_instruction(value, ok);
    if (!ok) {
        SC_REPORT_ERROR("MeshWrapper", "CONFIG: valor de MeshProgram desconocido");
        return;
    }

    mesh_.load_instr(0, 0, 0, instr);
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(0, 0);
    programmed_ = true;
}

// START (0x04, W) solo dispara ejecucion/lectura -- nunca reprograma la celda.
void MeshWrapper::handle_start_write(uint32_t value)
{
    start_ = value;
    if (value != 1) {
        return;
    }
    if (!programmed_) {
        SC_REPORT_ERROR("MeshWrapper", "START recibido antes de programar CONFIG");
        return;
    }

    status_ = 1;
    done_ = 0;

    // Margen validado para una fila PE_vector (bind directo, sin bridge) en
    // mesh/CGRA_Mesh_SmokeTest__TB.cpp: 2 ciclos entre escribir las entradas de
    // borde y leer el resultado en out_E.
    wait(2 * CLK_PERIOD_NS, SC_NS);

    result_ = out_E_.read();
    status_ = 0;
    done_ = 1;
}

// INPUT_DATA_BUFFER (0x10, W): 32 bytes = operando A (4 lanes int32, hacia in_W)
// concatenado con operando B (4 lanes int32, hacia in_N). Orden fijo A||B.
void MeshWrapper::handle_input_write(const unsigned char* data, unsigned int len)
{
    if (len != 32) {
        SC_REPORT_ERROR("MeshWrapper",
            "INPUT_DATA_BUFFER: se esperaban 32 bytes (2 operandos x 4 lanes x 4 bytes)");
        return;
    }

    int32_t a[4];
    int32_t b[4];
    std::memcpy(a, data, sizeof(a));
    std::memcpy(b, data + sizeof(a), sizeof(b));

    Link op_a, op_b;
    for (int i = 0; i < 4; ++i) {
        op_a[i] = a[i];
        op_b[i] = b[i];
    }

    in_W_.write(op_a);
    in_N_.write(op_b);
}

// OUTPUT_DATA_BUFFER (0x14, R): 16 bytes = resultado (4 lanes int32) capturado por
// handle_start_write en result_.
void MeshWrapper::handle_output_read(unsigned char* data, unsigned int len)
{
    if (len != 16) {
        SC_REPORT_ERROR("MeshWrapper",
            "OUTPUT_DATA_BUFFER: se esperaban 16 bytes (1 resultado x 4 lanes x 4 bytes)");
        return;
    }

    int32_t out[4];
    for (int i = 0; i < 4; ++i) {
        out[i] = result_[i].to_int();
    }
    std::memcpy(data, out, sizeof(out));
}

MeshWrapper::Instr MeshWrapper::build_instruction(uint32_t prog_value, bool& ok)
{
    switch (prog_value) {
        case PROGRAM_VECTOR_ADD: {
            ok = true;
            Instr instr;
            instr.opcode = OP_ADD;
            instr.src_a = SRC_WEST;
            instr.src_b = SRC_NORTH;
            instr.dst = DST_EAST;
            return instr;
        }
        default:
            ok = false;
            return Instr();
    }
}

void MeshWrapper::trace(sc_trace_file* tf) const
{
    sc_trace(tf, clk_, "clk");
    sc_trace(tf, rst_, "rst");
    sc_trace(tf, enable_, "enable");
    sc_trace(tf, in_N_, "in_N");
    sc_trace(tf, in_S_, "in_S");
    sc_trace(tf, in_W_, "in_W");
    sc_trace(tf, in_E_, "in_E");
    sc_trace(tf, out_N_, "out_N");
    sc_trace(tf, out_S_, "out_S");
    sc_trace(tf, out_W_, "out_W");
    sc_trace(tf, out_E_, "out_E");
    mesh_.trace(tf);
}
