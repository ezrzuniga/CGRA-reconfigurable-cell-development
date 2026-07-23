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
      mesh_("mesh", std::vector<CellKind>{
          CellKind::ROUTING, CellKind::MEMORY,   // fila 0: (0,0) (0,1)
          CellKind::SCALAR,  CellKind::VECTOR,   // fila 1: (1,0) (1,1)
      }),
      config_(0), start_(0), status_(0), done_(0), programmed_(false)
{
    mesh_.clk(clk_);
    mesh_.rst(rst_);
    mesh_.enable(enable_);
    for (int c = 0; c < COLS; c++) {
        mesh_.in_N[c](in_N_[c]);   mesh_.out_N[c](out_N_[c]);
        mesh_.in_S[c](in_S_[c]);   mesh_.out_S[c](out_S_[c]);
    }
    for (int r = 0; r < ROWS; r++) {
        mesh_.in_W[r](in_W_[r]);   mesh_.out_W[r](out_W_[r]);
        mesh_.in_E[r](in_E_[r]);   mesh_.out_E[r](out_E_[r]);
    }

    target_socket.register_b_transport(this, &MeshWrapper::b_transport);

    SC_THREAD(reset_thread);
}

PE_Memory_Mesh_Cell<32, 4>& MeshWrapper::memory_cell()
{
    return static_cast<PE_Memory_Mesh_Cell<32, 4>&>(mesh_.pe[1]);  // (0,1)
}

// Corre una unica vez al arrancar la simulacion -- no hay transaccion "RESET" en el
// mapa de registros, asi que el reset del mesh es un evento de arranque, no algo
// repetible por diseno.
void MeshWrapper::reset_thread()
{
    // in_*_[..] no se tocan aqui: ya arrancan en Link() (cero) por
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

// CONFIG (0x00, W): programa Escalar (1,0) y Vectorial (1,1) con la ecuacion del
// programa elegido -- ambas celdas quedan residentes (INSTR_MEM_SIZE=1, el PC hace
// loop en el unico slot) hasta la proxima escritura de CONFIG. Enrutamiento y
// Memoria NO se tocan aqui: PROGRAM_FULL_PIPELINE las programa recien en START
// (run_full_pipeline_dataflow), porque su secuencia depende de que el dato de
// entrada ya este en los bordes (ver handle_input_write).
//
// Importante (ver handle_input_write): PE_Scalar_Cell y el puerto NoC de
// PE_Memory_Mesh_Cell solo hablan lane 0 (broadcast, no elementwise real -- ver
// PE_Scalar_Cell.h y memory/PE_Memory_Mesh_Cell.h). Por eso el operando "a" (que
// necesita fidelidad por-lane real, ej. {1,2,3,4}) SIEMPRE se computa en Vectorial
// directo desde sus propios bordes reales (S, E), nunca atravesando Escalar o
// Memoria. El operando "b" es el unico que viaja por Enrutamiento/Memoria/Escalar
// en PROGRAM_FULL_PIPELINE -- valido en tanto b sea uniforme entre lanes (un
// escalar de verdad, ej. {1,1,1,1}), que es exactamente lo que Escalar y Memoria
// pueden transportar sin perder informacion.
void MeshWrapper::handle_config_write(uint32_t value)
{
    config_ = value;

    Instr scalar_instr, vector_instr;
    bool ok = true;
    switch (value) {
        case PROGRAM_VECTOR_ADD:
            // c = a + b, elementwise real: Vectorial calcula todo directo desde
            // sus dos bordes reales (S=a, E=b), sin pasar por Escalar. Mismo
            // resultado que la version <1,1> original (misma PE_vector, mismo
            // OP_ADD), para no romper el golden reference de
            // RiscvCore::test_vector_add(). Escalar queda en NOP (inactiva).
            vector_instr.opcode = OP_ADD;
            vector_instr.src_a = SRC_SOUTH;
            vector_instr.src_b = SRC_EAST;
            vector_instr.dst = DST_EAST;
            break;

        case PROGRAM_FULL_PIPELINE:
            // e = a + b*2, recorriendo las 4 celdas: b entra por el borde norte
            // real de Enrutamiento, se relevа por Memoria (round trip NoC, ver
            // run_full_pipeline_dataflow) y llega a Escalar por su borde norte
            // (interno); Escalar calcula b*2 (real, b es uniforme entre lanes) y
            // lo reenvia a Vectorial por el enlace interno; Vectorial suma su
            // borde sur real (a, con fidelidad por-lane real) con b*2 y expone el
            // resultado en su borde este real.
            scalar_instr.opcode = OP_MUL;
            scalar_instr.src_a = SRC_NORTH;
            scalar_instr.src_b = SRC_IMM;
            scalar_instr.imm = 2;
            scalar_instr.dst = DST_EAST;

            vector_instr.opcode = OP_ADD;
            vector_instr.src_a = SRC_SOUTH;
            vector_instr.src_b = SRC_WEST;
            vector_instr.dst = DST_EAST;
            break;

        default:
            ok = false;
            break;
    }

    if (!ok) {
        SC_REPORT_ERROR("MeshWrapper", "CONFIG: valor de MeshProgram desconocido");
        return;
    }

    mesh_.load_instr(1, 0, 0, scalar_instr);
    mesh_.load_instr(1, 1, 0, vector_instr);
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(1, 0);
    mesh_.clear_instr(1, 1);
    programmed_ = true;
}

// START (0x04, W) solo dispara ejecucion/lectura -- nunca reprograma Escalar ni
// Vectorial (eso es trabajo de CONFIG).
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

    if (config_ == PROGRAM_FULL_PIPELINE) {
        run_full_pipeline_dataflow();
    } else {
        // PROGRAM_VECTOR_ADD: solo Vectorial, bind directo sin bridge (ver
        // PE_Vector_Cell.h) -- mismo margen de 2 ciclos ya validado en
        // mesh/CGRA_Mesh_1x1_Test__TB.cpp para esta misma PE_vector.
        wait(2 * CLK_PERIOD_NS, SC_NS);
    }

    result_ = out_E_[1].read();
    status_ = 0;
    done_ = 1;
}

// Hace viajar el operando b (ya escrito por handle_input_write en el borde norte
// real de Enrutamiento, in_N_[0]) por las 4 celdas antes de que Escalar/Vectorial
// calculen el resultado: Enrutamiento -> Memoria (NoC, ida) -> Memoria -> NoC,
// vuelta) -> Enrutamiento -> Escalar. Secuencia y margenes identicos a los ya
// validados en mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp (Escenario 1), solo
// que orquestados aqui en vez de en un testbench.
void MeshWrapper::run_full_pipeline_dataflow()
{
    // Fase 1: Enrutamiento ctx0 relay N(real, b) -> E (hacia Memoria).
    mesh_.load_instr(0, 0, 0, make_routing_config_instr<32>(RC_NONE, RC_NONE, RC_FROM_N, RC_NONE));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(0, 0);

    // Fase 2: Memoria ctx0 (NoC->SRAM): captura b en sram[0].
    mesh_.load_instr(0, 1, 0, make_memory_field_instr<32>(MEM_FIELD_DIR, 1));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 0, make_memory_field_instr<32>(MEM_FIELD_MODE, 0));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 0, make_memory_field_instr<32>(MEM_FIELD_COUNT, 1));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 0, make_memory_field_instr<32>(MEM_FIELD_SRC_ADDR, 0));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 0, make_memory_field_instr<32>(MEM_FIELD_DST_ADDR, 0));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 0, make_memory_field_instr<32>(MEM_FIELD_START, 1));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(0, 1);

    for (int i = 0; i < 10 && !memory_cell().dma_done(); i++) {
        wait(CLK_PERIOD_NS, SC_NS);
    }
    // Fase 3: Memoria ctx1 (SRAM->NoC): reenvia sram[0] de vuelta hacia Enrutamiento.
    mesh_.load_instr(0, 1, 1, make_memory_field_instr<32>(MEM_FIELD_DIR, 0));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 1, make_memory_field_instr<32>(MEM_FIELD_MODE, 0));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 1, make_memory_field_instr<32>(MEM_FIELD_COUNT, 1));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 1, make_memory_field_instr<32>(MEM_FIELD_SRC_ADDR, 0));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 1, make_memory_field_instr<32>(MEM_FIELD_DST_ADDR, 0));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.load_instr(0, 1, 1, make_memory_field_instr<32>(MEM_FIELD_START, 1));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(0, 1);

    for (int i = 0; i < 10 && !memory_cell().dma_done(); i++) {
        wait(CLK_PERIOD_NS, SC_NS);
    }
    // Fase 4: Enrutamiento ctx1 relay E(desde Memoria) -> S (hacia Escalar).
    mesh_.load_instr(0, 0, 1, make_routing_config_instr<32>(RC_NONE, RC_FROM_E, RC_NONE, RC_NONE));
    wait(2 * CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(0, 0);

    // Escalar/Vectorial ya estan programados y corriendo (residentes desde
    // CONFIG); con a en in_W_[1] desde INPUT y b recien llegado por
    // Enrutamiento->Escalar, falta el mismo margen de asentamiento de la cadena
    // Escalar->Vectorial validado en el Escenario 2 del testbench de referencia.
    wait(2 * CLK_PERIOD_NS, SC_NS);
}

// INPUT_DATA_BUFFER (0x10, W): 32 bytes = operando a (4 lanes int32) concatenado
// con operando b (4 lanes int32). a siempre va al borde sur real de Vectorial
// (in_S_[1]) -- el unico camino que preserva fidelidad por-lane real en ambos
// programas (ver handle_config_write). b se escribe en los DOS posibles caminos
// de entrada -- borde este real de Vectorial (in_E_[1], usado por
// PROGRAM_VECTOR_ADD) y borde norte real de Enrutamiento (in_N_[0], usado por
// PROGRAM_FULL_PIPELINE) -- para que handle_input_write no dependa de que
// programa esta activo; cada programa solo lee el borde que realmente usa.
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

    in_S_[1].write(op_a);
    in_E_[1].write(op_b);
    in_N_[0].write(op_b);
}

// OUTPUT_DATA_BUFFER (0x14, R): 16 bytes = resultado (4 lanes int32), siempre
// tomado del borde este real de Vectorial (out_E_[1]), la celda final de ambos
// programas.
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

void MeshWrapper::trace(sc_trace_file* tf) const
{
    sc_trace(tf, clk_, "clk");
    sc_trace(tf, rst_, "rst");
    sc_trace(tf, enable_, "enable");
    for (int c = 0; c < COLS; c++) {
        sc_trace(tf, in_N_[c],  "in_N_" + std::to_string(c));
        sc_trace(tf, out_N_[c], "out_N_" + std::to_string(c));
        sc_trace(tf, in_S_[c],  "in_S_" + std::to_string(c));
        sc_trace(tf, out_S_[c], "out_S_" + std::to_string(c));
    }
    for (int r = 0; r < ROWS; r++) {
        sc_trace(tf, in_W_[r],  "in_W_" + std::to_string(r));
        sc_trace(tf, out_W_[r], "out_W_" + std::to_string(r));
        sc_trace(tf, in_E_[r],  "in_E_" + std::to_string(r));
        sc_trace(tf, out_E_[r], "out_E_" + std::to_string(r));
    }
    mesh_.trace(tf);
}
