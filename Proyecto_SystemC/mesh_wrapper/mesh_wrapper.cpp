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

// Margen por instruccion de los kernels aritmeticos (exp, softmax) y de
// run_matmul_dataflow: deja que el dato en los bordes se asiente y que el resultado
// propague por issue->ALU->writeback (latencia observada de ~1 ciclo) antes de
// avanzar. Solo es seguro con instrucciones IDEMPOTENTES -- las que escriben un
// registro que tambien leen se re-ejecutan con el margen extra y corrompen el
// resultado (ver el comentario de run_sum_reduction_dataflow, que por eso usa el
// patron de "exactamente 1 ciclo").
static const int SETTLE_CYCLES = 3;

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
      config_(0), start_(0), status_(0), done_(0), programmed_(false),
      sum_reduction_seed_(0)
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

//======================================================================
// Constructores breves de instrucciones (ver mesh_wrapper.h para el criterio)
//======================================================================

MeshWrapper::Instr MeshWrapper::make_instr(int op, int src_a, int reg_a, int src_b,
                                            int reg_b, int32_t imm, int dst, int reg_dst)
{
    Instr i;
    i.opcode = op;
    i.src_a = src_a; i.reg_a = reg_a;
    i.src_b = src_b; i.reg_b = reg_b;
    i.imm = imm;
    i.dst = dst;     i.reg_dst = reg_dst;
    return i;
}

// reg[ra] op reg[rb] -> reg[rd]
MeshWrapper::Instr MeshWrapper::alu_rr(int op, int ra, int rb, int rd) {
    return make_instr(op, SRC_REG, ra, SRC_REG, rb, 0, DST_REG, rd);
}
// reg[ra] op imm -> reg[rd]
MeshWrapper::Instr MeshWrapper::alu_ri(int op, int ra, int32_t imm, int rd) {
    return make_instr(op, SRC_REG, ra, SRC_IMM, 0, imm, DST_REG, rd);
}
// imm op reg[rb] -> reg[rd]   (inmediato como operando IZQUIERDO, para 0 - x / 2 - x)
MeshWrapper::Instr MeshWrapper::alu_ir(int op, int32_t imm, int rb, int rd) {
    return make_instr(op, SRC_IMM, 0, SRC_REG, rb, imm, DST_REG, rd);
}
// borde op reg[rb] -> reg[rd]
MeshWrapper::Instr MeshWrapper::alu_sr(int op, int src, int rb, int rd) {
    return make_instr(op, src, 0, SRC_REG, rb, 0, DST_REG, rd);
}
// borde op borde -> reg[rd]
MeshWrapper::Instr MeshWrapper::alu_ss(int op, int src_a, int src_b, int rd) {
    return make_instr(op, src_a, 0, src_b, 0, 0, DST_REG, rd);
}
// imm -> reg[rd]
MeshWrapper::Instr MeshWrapper::alu_mov_imm(int32_t imm, int rd) {
    return make_instr(OP_MOV, SRC_IMM, 0, SRC_REG, 0, imm, DST_REG, rd);
}
// borde -> reg[rd]
MeshWrapper::Instr MeshWrapper::alu_mov_src(int src, int rd) {
    return make_instr(OP_MOV, src, 0, SRC_REG, 0, 0, DST_REG, rd);
}
// reg[ra] -> borde. Ademas de emitir, es la instruccion "protectora" tipica: solo lee
// un registro, asi que puede quedar residente mientras el wrapper reescribe los
// bordes per-lane sin que recompute nada (ver run_matmul_dataflow).
MeshWrapper::Instr MeshWrapper::out_reg(int ra, int dst) {
    return make_instr(OP_MOV, SRC_REG, ra, SRC_REG, 0, 0, dst, 0);
}
// reg[ra] op reg[rb] -> borde
MeshWrapper::Instr MeshWrapper::out_rr(int op, int ra, int rb, int dst) {
    return make_instr(op, SRC_REG, ra, SRC_REG, rb, 0, dst, 0);
}
// reg[ra] op imm -> borde
MeshWrapper::Instr MeshWrapper::out_ri(int op, int ra, int32_t imm, int dst) {
    return make_instr(op, SRC_REG, ra, SRC_IMM, 0, imm, dst, 0);
}

void MeshWrapper::step_cell(int row, int col, const Instr& instr)
{
    mesh_.load_instr(row, col, 0, instr);
    wait(SETTLE_CYCLES * CLK_PERIOD_NS, SC_NS);
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

        case PROGRAM_SUM_REDUCTION:
            // total = seed + sum(v[0..6]). Escalar se reprograma varias veces
            // durante START (run_sum_reduction_dataflow) -- lo que se cargue aqui
            // para Escalar no importa, queda pisado de inmediato. Vectorial solo
            // reenvia (MOV) lo que le llegue de Escalar hacia su borde este real.
            vector_instr.opcode = OP_MOV;
            vector_instr.src_a = SRC_WEST;
            vector_instr.dst = DST_EAST;
            break;

        case PROGRAM_MATMUL:
        case PROGRAM_EXP_VEC:
        case PROGRAM_SOFTMAX:
            // Vectorial se reprograma varias veces durante START
            // (run_matmul_dataflow / run_exp_vec_dataflow); lo que se cargue aqui
            // queda pisado de inmediato. Escalar/Enrutamiento/Memoria no participan.
            // Solo hace falta dejar programmed_=true (via el load/clear generico de
            // abajo).
            scalar_instr.opcode = OP_NOP;
            vector_instr.opcode = OP_NOP;
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
    } else if (config_ == PROGRAM_SUM_REDUCTION) {
        run_sum_reduction_dataflow();
    } else if (config_ == PROGRAM_MATMUL) {
        run_matmul_dataflow();
    } else if (config_ == PROGRAM_EXP_VEC) {
        run_exp_vec_dataflow();
    } else if (config_ == PROGRAM_SOFTMAX) {
        run_softmax_dataflow();
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

// Reduccion por suma: total = seed + sum(sum_reduction_vec_[0..6]). El seed viaja
// Enrutamiento->Memoria->Enrutamiento->Escalar exactamente igual que "b" en
// run_full_pipeline_dataflow (mismas 4 fases, mismos margenes ya validados);
// despues Escalar se reprograma una vez por cada elemento (mas dos veces mas, para
// sembrar reg0 con el seed y para reenviar el total a Vectorial).
//
// Importante (encontrado depurando esta funcion, ver historia de commits): cada
// mesh_.load_instr(...) que este codigo hace ya ejecuta en el MISMO flanco de clk
// en el que se carga (load_program() e issue() corren en el mismo flanco de
// clk.pos(), y en la practica load_program() gana esa carrera) -- por eso el resto
// de este archivo (CONFIG, fases 1-4) siempre usa exactamente 1 wait(CLK_PERIOD_NS)
// tras cada load_instr antes de la siguiente accion. Un segundo flanco de margen
// (el patron "load + wait + clear + wait" que llegaron a tener versiones
// anteriores de esta funcion) hace que la MISMA instruccion se ejecute DOS veces
// -- inofensivo para instrucciones idempotentes (rutear, poner un campo de
// contexto), pero fatal para un acumulador: cada elemento terminaba sumandose 2
// veces. La fase 6 de abajo evita el problema de raiz: en vez de dejar una unica
// instruccion residente y cambiar el dato de entrada por fuera (que ademas exige
// que el dato este listo exactamente en el ciclo correcto), carga una instruccion
// NUEVA por cada elemento con el valor ya empaquetado en el campo imm -- mismo
// patron de "load + exactamente 1 ciclo" que ya se usa en todos lados, repetido 7
// veces, sin depender de sincronizar un puerto externo con el ciclo de la ALU.
void MeshWrapper::run_sum_reduction_dataflow()
{
    // Fase 1: Enrutamiento ctx0 relay N(real, seed) -> E (hacia Memoria).
    mesh_.load_instr(0, 0, 0, make_routing_config_instr<32>(RC_NONE, RC_NONE, RC_FROM_N, RC_NONE));
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(0, 0);

    // Fase 2: Memoria ctx0 (NoC->SRAM): captura el seed en sram[0].
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

    // Fase 4: Enrutamiento ctx1 relay E(desde Memoria) -> S (hacia Escalar). A
    // diferencia de la fase 6 (acumulador, ver comentario de cabecera), rutear no
    // es autorreferente -- darle margen extra para que la senal se asiente por la
    // malla (Enrutamiento -> Escalar) es inofensivo, no duplica ninguna suma.
    mesh_.load_instr(0, 0, 1, make_routing_config_instr<32>(RC_NONE, RC_FROM_E, RC_NONE, RC_NONE));
    wait(2 * CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(0, 0);

    // Fase 5: Escalar reg0 = seed (0 + norte, ya disponible via Enrutamiento). Por
    // la misma razon que la fase 4, un margen extra aqui es seguro: load_seed no
    // acumula, solo sobreescribe reg0 con el mismo seed cada vez que se ejecute.
    Instr load_seed;
    load_seed.opcode = OP_ADD;
    load_seed.src_a = SRC_IMM;
    load_seed.imm = 0;
    load_seed.src_b = SRC_NORTH;
    load_seed.dst = DST_REG;
    load_seed.reg_dst = 0;
    mesh_.load_instr(1, 0, 0, load_seed);
    wait(2 * CLK_PERIOD_NS, SC_NS);

    // Fase 6: Escalar acumula los 7 elementos del vector: una instruccion NUEVA
    // por elemento (reg0 = reg0 + imm(v[i])), cada una ejecutada exactamente un
    // ciclo antes de pasar a la siguiente.
    for (int i = 0; i < SUM_REDUCTION_VECTOR_LEN; ++i) {
        Instr add_elem;
        add_elem.opcode = OP_ADD;
        add_elem.src_a = SRC_REG;
        add_elem.reg_a = 0;
        add_elem.src_b = SRC_IMM;
        add_elem.imm = sum_reduction_vec_[i];
        add_elem.dst = DST_REG;
        add_elem.reg_dst = 0;
        mesh_.load_instr(1, 0, 0, add_elem);
        wait(CLK_PERIOD_NS, SC_NS);
    }

    // Fase 7: Escalar reenvia el total (reg0 + 0) hacia Vectorial (enlace interno).
    Instr emit;
    emit.opcode = OP_ADD;
    emit.src_a = SRC_REG;
    emit.reg_a = 0;
    emit.src_b = SRC_IMM;
    emit.imm = 0;
    emit.dst = DST_EAST;
    mesh_.load_instr(1, 0, 0, emit);
    wait(CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(1, 0);

    // Vectorial ya programado desde CONFIG (MOV oeste->este); margen de
    // asentamiento igual al ya validado para la cadena Escalar->Vectorial.
    wait(2 * CLK_PERIOD_NS, SC_NS);
}

// Multiplicacion matricial 2x2: C = A * B, computada enteramente en la celda
// Vectorial (1,1) usando SIMD real por lane. Se aplana C row-major en los 4 lanes
// del resultado:  lane0=C00, lane1=C01, lane2=C10, lane3=C11.
//
// C[i][j] = A[i][0]*B[0][j] + A[i][1]*B[1][j], asi que cada lane es una suma de 2
// productos. Reagrupando por k (la dimension contraida) los 4 lanes comparten el
// mismo patron, y cada termino_k es un producto ELEMENTWISE (lane-locked, sin
// cruce entre lanes) de dos vectores:
//
//   termino_0[lane] = Avec0[lane] * Bvec0[lane]
//   termino_1[lane] = Avec1[lane] * Bvec1[lane]
//   C_flat = termino_0 + termino_1
//
// con los operandos reordenados por lane (trabajo de marshaling, no de computo --
// solo replica/mueve valores de entrada a lanes, como haria un DMA scatter):
//   Avec0 = {A00, A00, A10, A10}   Bvec0 = {B00, B01, B00, B01}
//   Avec1 = {A01, A01, A11, A11}   Bvec1 = {B10, B11, B10, B11}
//
// La ALU vectorial hace las 8 multiplicaciones (4 lanes x 2 pasos) y las 4 sumas.
// Cada paso k pone su par (Avec_k, Bvec_k) en los dos bordes reales per-lane de
// Vectorial (S y E) -- estos writes salen del mismo hilo initiator que
// handle_input_write, asi que siguen siendo un unico driver logico de in_S_/in_E_
// (no dispara el E115 de sc_signal). Reusa el patron "load + exactamente 1
// wait(CLK_PERIOD)" validado en run_sum_reduction_dataflow: la suma acumuladora
// (reg0 += reg1) es la unica instruccion no idempotente, y se la pisa de inmediato
// con la de emision para que ejecute una sola vez.
void MeshWrapper::run_matmul_dataflow()
{
    const int32_t* A = matmul_a_;  // {A00, A01, A10, A11}
    const int32_t* B = matmul_b_;  // {B00, B01, B10, B11}

    Link Avec0, Bvec0, Avec1, Bvec1;
    Avec0[0] = A[0]; Avec0[1] = A[0]; Avec0[2] = A[2]; Avec0[3] = A[2];
    Bvec0[0] = B[0]; Bvec0[1] = B[1]; Bvec0[2] = B[0]; Bvec0[3] = B[1];
    Avec1[0] = A[1]; Avec1[1] = A[1]; Avec1[2] = A[3]; Avec1[3] = A[3];
    Bvec1[0] = B[2]; Bvec1[1] = B[3]; Bvec1[2] = B[2]; Bvec1[3] = B[3];

    // Margen de asentamiento por paso: garantiza que el dato en los bordes
    // per-lane se asiente y que el resultado propague por issue->ALU->writeback
    // (latencia observada de ~1 ciclo) antes de avanzar. Todas las instrucciones
    // de este dataflow son idempotentes (ninguna acumula sobre si misma), asi que
    // un margen holgado nunca corrompe.
    const int SETTLE = 3;

    // El punto delicado es CAMBIAR los bordes per-lane entre pasos: mientras la
    // instruccion residente lea los bordes (S/E), reescribirlos la haria
    // recomputar su destino con datos nuevos. La disciplina de abajo evita eso
    // dejando SIEMPRE residente una instruccion que solo lee REGISTROS (inmune a
    // los bordes) durante cada cambio de bordes:
    //
    //   1. bordes=Avec0/Bvec0, reg0 = S*E            (= termino_0)
    //   2. reg2 = reg0            <- resiliente: solo lee reg0, no bordes
    //   3. bordes=Avec1/Bvec1 (seguro: 'reg2=reg0' residente no toca reg0)
    //      reg0 = S*E             (= termino_1)
    //   4. East = reg2 + reg0     (= termino_0 + termino_1 = C aplanada)

    // Paso 1: reg0 = South * East = termino_0.
    in_S_[1].write(Avec0);
    in_E_[1].write(Bvec0);
    Instr mul;
    mul.opcode = OP_MUL; mul.src_a = SRC_SOUTH; mul.src_b = SRC_EAST;
    mul.dst = DST_REG; mul.reg_dst = 0;
    mesh_.load_instr(1, 1, 0, mul);
    wait(SETTLE * CLK_PERIOD_NS, SC_NS);

    // Paso 2: reg2 = reg0 (guarda termino_0 leyendo solo registros). Queda
    // residente y protege reg0 durante el cambio de bordes del paso 3.
    Instr save;
    save.opcode = OP_ADD; save.src_a = SRC_REG; save.reg_a = 0;
    save.src_b = SRC_IMM; save.imm = 0; save.dst = DST_REG; save.reg_dst = 2;
    mesh_.load_instr(1, 1, 0, save);
    wait(SETTLE * CLK_PERIOD_NS, SC_NS);

    // Paso 3: bordes -> Avec1/Bvec1 (seguro, 'save' no lee bordes), luego reg0 =
    // South * East = termino_1. termino_0 sigue a salvo en reg2.
    in_S_[1].write(Avec1);
    in_E_[1].write(Bvec1);
    mesh_.load_instr(1, 1, 0, mul);   // mismo mul (reg0 = S*E), ahora con Avec1/Bvec1
    wait(SETTLE * CLK_PERIOD_NS, SC_NS);

    // Paso 4: East = reg2 + reg0 = C aplanada, en una sola instruccion idempotente
    // (lee registros, rutea al borde este real de Vectorial, no modifica nada).
    Instr emit;
    emit.opcode = OP_ADD;
    emit.src_a = SRC_REG; emit.reg_a = 2;
    emit.src_b = SRC_REG; emit.reg_b = 0;
    emit.dst = DST_EAST;
    mesh_.load_instr(1, 1, 0, emit);
    wait(SETTLE * CLK_PERIOD_NS, SC_NS);
    mesh_.clear_instr(1, 1);

    wait(2 * CLK_PERIOD_NS, SC_NS);
}

// e^u elementwise sobre los 4 lanes, en punto fijo Q4.12 (ver EXP_Q_* en el .h),
// computado enteramente en la celda Vectorial (1,1). Es el primer programa del
// catalogo que evalua una funcion trascendente, algo que el ISA no ofrece: no hay
// exp, ni division, ni punto flotante -- solo la aritmetica entera de ALU_vector.h.
//
// La identidad que lo hace posible es la range reduction base-2:
//
//     e^u = 2^(u * log2(e)) = 2^k * 2^f      con t = u*log2(e), k = floor(t), f = t-k
//
// que parte el problema en dos mitades que el ISA SI sabe hacer:
//   - 2^k con k entero <= 0 es exactamente un shift a la derecha (OP_SRA). Y el
//     shamt de ALU_vector sale de b[i], o sea POR LANE, asi que los 4 lanes se
//     desplazan cantidades distintas en UNA instruccion -- justo lo que hace falta,
//     porque cada lane tiene su propio exponente.
//   - 2^f con f en [0,1) es un polinomio grado 3 por Horner (OP_MUL/OP_SRA/OP_ADD).
//
// Las 23 instrucciones son lane-locked (cero cruce entre lanes) y, mas importante,
// TODAS son idempotentes: cada una escribe un registro que no lee. Eso es lo que
// permite usar el margen holgado de SETTLE ciclos del estilo de run_matmul_dataflow
// en vez del "exactamente 1 ciclo" fragil de run_sum_reduction_dataflow. Costo:
// Horner no puede acumular en un solo registro (p = p*f>>12 + c se leeria a si
// mismo), asi que rota por tres -- r4 -> r5 -> r6 -> r4. Tres instrucciones por
// termino en vez de dos, a cambio de inmunidad total a los margenes de tiempo.
//
// Asignacion de registros (NUM_REGS=8):
//   r0 = u saturado    r1 = shift = -k    r2 = t    r3 = f
//   r4 = p (acumulador Horner)            r5, r6, r7 = temporales
void MeshWrapper::emit_exp_sequence(int reg_u, int reg_out)
{
    // Registros temporales. reg_u y reg_out los elige el llamador; el resto del banco
    // (NUM_REGS=8) se usa aca como scratch, asi que quien llame no puede tener nada
    // vivo fuera de esos dos.
    const int T1 = 1, T2 = 2, T3 = 3, P = 4, T5 = 5, T6 = 6;

    // Fase 1: saturar por abajo, T? = max(u, EXP_Q_U_MIN). El ISA no tiene MAX ni
    // saltos, asi que se hace branchless explotando que OP_SRA por 31 difunde el bit
    // de signo a los 32 bits (mascara de todos-unos si es negativo, cero si no):
    //     d = u - UMIN ;  m = d>>31 ;  max = u - (d & m)
    // Si u >= UMIN: d >= 0, m = 0, max = u. Si u < UMIN: m = -1, d&m = d,
    // max = u - (u - UMIN) = UMIN. Exacto, sin ramas, 4 instrucciones.
    step_cell(1, 1, alu_ri(OP_SUB, reg_u, EXP_Q_U_MIN, T1));   // d
    step_cell(1, 1, alu_ri(OP_SRA, T1, 31, T2));               // m
    step_cell(1, 1, alu_rr(OP_AND, T1, T2, T3));               // d & m
    step_cell(1, 1, alu_rr(OP_SUB, reg_u, T3, P));             // max(u, UMIN)

    // Fase 2: saturar por arriba, min(P, 0). Contra la constante 0 el mismo truco
    // colapsa a dos instrucciones: min(x,0) = x & (x>>31).
    step_cell(1, 1, alu_ri(OP_SRA, P, 31, T1));                // m
    step_cell(1, 1, alu_rr(OP_AND, P, T1, reg_u));             // u en [UMIN, 0]

    // Fase 3: t = u * log2(e). El producto de dos Q4.12 es Q8.24, de ahi el >>12 para
    // volver a Q4.12. Peor caso |u * LOG2E| = 1.9e8, el 9% de int32 -- OP_MUL trunca a
    // 32 bits sin avisar (no hay MULH), asi que el margen importa.
    step_cell(1, 1, alu_ri(OP_MUL, reg_u, EXP_Q_LOG2E, T1));   // u*log2(e)  (Q8.24)
    step_cell(1, 1, alu_ri(OP_SRA, T1, EXP_Q_FRAC_BITS, T2));  // t          (Q4.12)

    // Fase 4: partir t en k = floor(t) y f = t - k. OP_SRA es floor aritmetico (no
    // truncamiento hacia cero), asi que funciona directo con t negativo: para t = -3.7
    // da k = -4 y f = 0.3, que es lo que se necesita para que f caiga en [0,1). Por la
    // misma razon el AND con la mascara fraccionaria da f en complemento a dos sin
    // correccion extra.
    step_cell(1, 1, alu_ri(OP_SRA, T2, EXP_Q_FRAC_BITS, T6));  // k = floor(t) <= 0
    step_cell(1, 1, alu_ir(OP_SUB, 0, T6, T1));                // shift = -k >= 0
    step_cell(1, 1, alu_ri(OP_AND, T2, EXP_Q_FRAC_MASK, T3));  // f en [0, 4095]

    // Fase 5: 2^f por Horner grado 3,
    //     p = ((c3*f>>12 + c2)*f>>12 + c1)*f>>12 + c0
    // rotando P -> T5 -> T6 -> P por termino. La forma natural (acumular en un solo
    // registro) se leeria a si misma y seria no-idempotente, lo que obligaria al patron
    // fragil de "exactamente 1 ciclo" de run_sum_reduction_dataflow. Rotando, TODA
    // instruccion de esta secuencia escribe un registro que no lee, y el margen holgado
    // de step_cell es seguro. Cuesta 3 instrucciones por termino en vez de 2.
    step_cell(1, 1, alu_mov_imm(EXP_Q_C3, P));
    const int32_t horner_coef[3] = { EXP_Q_C2, EXP_Q_C1, EXP_Q_C0 };
    for (int i = 0; i < 3; ++i) {
        step_cell(1, 1, alu_rr(OP_MUL, P, T3, T5));                 // p * f
        step_cell(1, 1, alu_ri(OP_SRA, T5, EXP_Q_FRAC_BITS, T6));   // (p*f) >> 12
        step_cell(1, 1, alu_ri(OP_ADD, T6, horner_coef[i], P));     // + coeficiente
    }

    // Fase 6: e^u = 2^f >> shift. Una sola instruccion aunque cada lane tenga un
    // exponente distinto, porque el shamt sale de un REGISTRO (T1) y ALU_vector lo
    // toma de b[i], o sea POR LANE. Es la pieza del ISA que hace viable este kernel.
    step_cell(1, 1, alu_rr(OP_SRA, P, T1, reg_out));
}

// e^u elementwise sobre los 4 lanes, en punto fijo Q4.12 (ver EXP_Q_* en el .h),
// computado enteramente en la celda Vectorial (1,1). Es el primer programa del
// catalogo que evalua una funcion trascendente, algo que el ISA no ofrece: no hay exp,
// ni division, ni punto flotante -- solo la aritmetica entera de ALU_vector.h.
//
// La identidad que lo hace posible es la range reduction base-2:
//
//     e^u = 2^(u * log2(e)) = 2^k * 2^f     con t = u*log2(e), k = floor(t), f = t-k
//
// que parte el problema en dos mitades que el ISA SI sabe hacer:
//   - 2^k con k entero <= 0 es exactamente un shift a la derecha (OP_SRA), y el shamt
//     de ALU_vector sale de b[i], o sea POR LANE: los 4 lanes se desplazan cantidades
//     distintas en UNA instruccion, que es justo lo que hace falta porque cada lane
//     tiene su propio exponente.
//   - 2^f con f en [0,1) es un polinomio grado 3 por Horner.
//
// El cuerpo vive en emit_exp_sequence() porque PROGRAM_SOFTMAX lo reusa tal cual.
void MeshWrapper::run_exp_vec_dataflow()
{
    // u entra por el borde sur real de Vectorial. Este write sale del mismo hilo
    // initiator que handle_input_write, asi que in_S_[1] sigue teniendo un unico
    // driver logico (no dispara el E115 de sc_signal).
    Link u_link;
    for (int lane = 0; lane < 4; ++lane) u_link[lane] = exp_u_[lane];
    in_S_[1].write(u_link);

    const int R_U = 0, R_E = 7;
    step_cell(1, 1, alu_mov_src(SRC_SOUTH, R_U));   // capturar u del borde
    emit_exp_sequence(R_U, R_E);
    step_cell(1, 1, out_reg(R_E, DST_EAST));        // exponer e^u en el borde este real

    mesh_.clear_instr(1, 1);
    wait(2 * CLK_PERIOD_NS, SC_NS);
}

// Softmax sobre los 4 lanes en Q4.12: y_i = e^(x_i - max) / sum_j e^(x_j - max).
//
// Es el unico programa del catalogo que reparte el computo entre DOS celdas por
// razones arquitectonicas y no por conveniencia, con ida y vuelta real sobre el
// enlace interno Escalar<->Vectorial:
//
//   Vectorial (1,1): las dos reducciones (max y suma), la resta del maximo, los 4
//                    exponenciales y la normalizacion final. Todo lane-parallel.
//   Escalar   (1,0): el reciproco 1/S. S es UN numero -- calcularlo en Vectorial
//                    serian 4 lanes repitiendo el mismo trabajo.
//
// ---- Por que las reducciones van por butterfly y no por Escalar ----
//
// Softmax necesita dos reducciones cross-lane (el maximo y la suma), y la ALU
// vectorial es lane-locked. La opcion "obvia" seria mandarlas a Escalar, que es una
// celda escalar y ya sabe acumular (ver run_sum_reduction_dataflow). No se hace, y el
// motivo es la asimetria del enlace: Escalar solo transporta lane 0 (ver
// PE_Scalar_Cell.h), asi que meterle los 4 lanes de Vectorial exige serializarlos de
// a uno -- 4 viajes de ida y vuelta por reduccion.
//
// El butterfly los hace en 2 pasos DENTRO de Vectorial, y ademas deja el resultado ya
// difundido a los 4 lanes, que es la forma en que hace falta despues:
//
//   paso 1: parejas (0,1) y (2,3)   ->  {A01, A01, A23, A23}
//   paso 2: rotar 2 lanes           ->  {A,   A,   A,   A  }
//
// La permutacion de lanes la hace el wrapper releyendo el borde este y reescribiendo
// los dos bordes reales -- marshaling puro, como un DMA scatter; el computo (los max
// y las sumas) lo hace la ALU. Mismo criterio ya usado en run_matmul_dataflow.
//
// La direccion FACIL del enlace, en cambio, se aprovecha entera: los dos escalares que
// softmax necesita difundir a los 4 lanes son justo los que produce Escalar, y su
// salida hace broadcast de lane 0 a las VLEN lanes por construccion.
//
// ---- Disciplina de idempotencia ----
//
// Igual que en run_matmul_dataflow y emit_exp_sequence, toda instruccion escribe un
// registro/borde que no lee, asi que el margen holgado de step_cell nunca corrompe.
// El punto delicado son los cambios de bordes: mientras la instruccion residente lea
// S o E, reescribirlos la haria recomputar con datos nuevos. Por eso antes de CADA
// reescritura queda residente una instruccion que solo lee registros -- y aca esa
// instruccion es siempre un out_reg(...), que de paso es la que expone el valor que el
// wrapper necesita leer para permutarlo. Las dos funciones en una.
void MeshWrapper::run_softmax_dataflow()
{
    // --- Registros de Vectorial (NUM_REGS=8) -------------------------------
    // Se reusan entre fases; los nombres valen para la fase en la que aparecen.
    const int R_D    = 0;   // temporal del max branchless / suma parcial / producto final
    const int R_M    = 1;   // mascara de signo del max / total S
    const int R_T    = 2;   // temporal del max branchless
    const int R_ACC  = 3;   // resultado de cada paso de la reduccion por maximo
    const int R_KEEP = 4;   // copia protegida del maximo
    const int R_U    = 0;   // u = x - max  (alias de R_D, ya muerto en ese punto)
    const int R_E    = 7;   // e = exp(u), tiene que sobrevivir hasta la fase G
    const int R_SUM  = 1;   // S = sum(e)   (alias de R_M)

    // --- Registros de Escalar ----------------------------------------------
    const int S_S    = 0;   // S recibido de Vectorial
    const int S_FLAG = 1;   // 1 si S >= 2, si no 0 (sirve directo como shamt)
    const int S_SN   = 2;   // S normalizado a [1,2]
    const int S_TA   = 3;   // temporales del Newton-Raphson
    const int S_TB   = 4;
    const int S_R    = 5;   // r, el reciproco en construccion
    const int S_TC   = 6;

    // Permutaciones de lane de las reducciones butterfly. Puro movimiento de datos.
    auto swap_pairs = [](const Link& v) {          // {a,b,c,d} -> {b,a,d,c}
        Link r; r[0] = v[1]; r[1] = v[0]; r[2] = v[3]; r[3] = v[2]; return r;
    };
    auto rot2 = [](const Link& v) {                // {a,b,c,d} -> {c,d,a,b}
        Link r; r[0] = v[2]; r[1] = v[3]; r[2] = v[0]; r[3] = v[1]; return r;
    };

    // Un paso de maximo elementwise entre los dos bordes reales de Vectorial, sin
    // ramas (el ISA no tiene saltos ni MAX):
    //     d = S - E ;  m = d>>31 ;  max = S - (d & m)
    // Si S >= E: d >= 0, m = 0, max = S. Si S < E: m = -1, d&m = d, max = S-(S-E) = E.
    auto max_step = [&]() {
        step_cell(1, 1, alu_ss(OP_SUB, SRC_SOUTH, SRC_EAST, R_D));   // d
        step_cell(1, 1, alu_ri(OP_SRA, R_D, 31, R_M));               // m
        step_cell(1, 1, alu_rr(OP_AND, R_D, R_M, R_T));              // d & m
        step_cell(1, 1, alu_sr(OP_SUB, SRC_SOUTH, R_T, R_ACC));      // max
    };

    Link x;
    for (int lane = 0; lane < 4; ++lane) x[lane] = softmax_x_[lane];

    //--------------------------------------------------------------------
    // Fase A: max = max(x_0..x_3), por butterfly de 2 pasos.
    //--------------------------------------------------------------------
    in_S_[1].write(x);
    in_E_[1].write(swap_pairs(x));
    max_step();                                       // {M01, M01, M23, M23}

    step_cell(1, 1, out_reg(R_ACC, DST_EAST));        // expone y protege
    {
        const Link partial = out_E_[1].read();
        in_S_[1].write(partial);
        in_E_[1].write(rot2(partial));
    }
    max_step();                                       // {M, M, M, M}

    //--------------------------------------------------------------------
    // Fase B: u = x - max. Nunca positivo por construccion, que es justo la
    // precondicion que estabiliza el exponencial y, mas abajo, la que acota S.
    //--------------------------------------------------------------------
    step_cell(1, 1, alu_ri(OP_ADD, R_ACC, 0, R_KEEP));  // protege el max (solo lee reg)
    in_S_[1].write(x);
    step_cell(1, 1, alu_sr(OP_SUB, SRC_SOUTH, R_KEEP, R_U));

    //--------------------------------------------------------------------
    // Fase C: e_i = exp(u_i), los 4 lanes en paralelo. Reusa tal cual el kernel ya
    // validado por PROGRAM_EXP_VEC -- ver emit_exp_sequence.
    //--------------------------------------------------------------------
    emit_exp_sequence(R_U, R_E);

    //--------------------------------------------------------------------
    // Fase D: S = sum(e_i), por butterfly de 2 pasos. El out_reg de cada paso hace
    // doble trabajo: expone el valor para que el wrapper lo permute y, por leer solo
    // registros, protege el resultado durante la reescritura de bordes.
    //--------------------------------------------------------------------
    step_cell(1, 1, out_reg(R_E, DST_EAST));
    {
        const Link ev = out_E_[1].read();
        in_S_[1].write(ev);
        in_E_[1].write(swap_pairs(ev));
    }
    step_cell(1, 1, alu_ss(OP_ADD, SRC_SOUTH, SRC_EAST, R_D));   // {e0+e1, .., e2+e3, ..}

    step_cell(1, 1, out_reg(R_D, DST_EAST));
    {
        const Link partial = out_E_[1].read();
        in_S_[1].write(partial);
        in_E_[1].write(rot2(partial));
    }
    step_cell(1, 1, alu_ss(OP_ADD, SRC_SOUTH, SRC_EAST, R_SUM));  // S, difundido

    //--------------------------------------------------------------------
    // Fase E: Vectorial le pasa S a Escalar por el enlace interno. Esta instruccion
    // queda residente durante TODA la fase F: al leer solo un registro, sostiene S
    // estable en el enlace mientras Escalar itera.
    //--------------------------------------------------------------------
    step_cell(1, 1, out_reg(R_SUM, DST_WEST));

    //--------------------------------------------------------------------
    // Fase F: Escalar calcula r = 1/S por Newton-Raphson, r <- r*(2 - S*r).
    // Solo OP_MUL y OP_SUB; el ISA no tiene division.
    //--------------------------------------------------------------------
    step_cell(1, 0, alu_mov_src(SRC_EAST, S_S));    // S (Escalar toma lane 0)

    // Normalizar S de [1,4] a [1,2]. OP_SLT devuelve 0/1, que sirve DIRECTO como
    // shamt de OP_SRA -- no hace falta convertir la comparacion en un salto (que el
    // ISA no tiene). Newton-Raphson duplica bits correctos por iteracion, asi que
    // arrancar de un rango 2x en vez de 4x ahorra iteraciones enteras.
    step_cell(1, 0, alu_ir(OP_SLT, EXP_Q_TWO - 1, S_S, S_FLAG));  // flag = (S >= 2)
    step_cell(1, 0, alu_rr(OP_SRA, S_S, S_FLAG, S_SN));           // Sn = S >> flag

    // Semilla lineal r0 = A - B*Sn (minimax sobre [1,2], ver SOFTMAX_NR_SEED_*).
    step_cell(1, 0, alu_ri(OP_MUL, S_SN, SOFTMAX_NR_SEED_B, S_TA));
    step_cell(1, 0, alu_ri(OP_SRA, S_TA, EXP_Q_FRAC_BITS, S_TB));
    step_cell(1, 0, alu_ir(OP_SUB, SOFTMAX_NR_SEED_A, S_TB, S_R));

    for (int it = 0; it < SOFTMAX_NR_ITERS; ++it) {
        step_cell(1, 0, alu_rr(OP_MUL, S_SN, S_R, S_TA));               // Sn*r
        step_cell(1, 0, alu_ri(OP_SRA, S_TA, EXP_Q_FRAC_BITS, S_TB));
        step_cell(1, 0, alu_ir(OP_SUB, EXP_Q_TWO, S_TB, S_TC));         // 2 - Sn*r
        step_cell(1, 0, alu_rr(OP_MUL, S_R, S_TC, S_TA));               // r*(2 - Sn*r)
        step_cell(1, 0, alu_ri(OP_SRA, S_TA, EXP_Q_FRAC_BITS, S_R));
    }

    // Desnormalizar (r = r_norm >> flag) y devolverselo a Vectorial en la misma
    // instruccion. La salida de Escalar difunde lane 0 a las 4 lanes, que es
    // exactamente como Vectorial necesita r.
    step_cell(1, 0, out_rr(OP_SRA, S_R, S_FLAG, DST_EAST));

    //--------------------------------------------------------------------
    // Fase G: y_i = e_i * r. La instruccion de emision de Escalar sigue residente,
    // sosteniendo r en el borde oeste de Vectorial.
    //--------------------------------------------------------------------
    step_cell(1, 1, alu_sr(OP_MUL, SRC_WEST, R_E, R_D));            // r * e  (Q8.24)
    step_cell(1, 1, out_ri(OP_SRA, R_D, EXP_Q_FRAC_BITS, DST_EAST)); // y     (Q4.12)

    mesh_.clear_instr(1, 0);
    mesh_.clear_instr(1, 1);
    wait(2 * CLK_PERIOD_NS, SC_NS);
}

// INPUT_DATA_BUFFER (0x10, W): 32 bytes, interpretados segun el programa activo
// (config_, ya establecido por la escritura de CONFIG que precede a esta segun el
// protocolo -- ver CSR_DMA::dma_controller()):
//  - PROGRAM_VECTOR_ADD / PROGRAM_FULL_PIPELINE: operando a (4 lanes int32)
//    concatenado con operando b (4 lanes int32). a siempre va al borde sur real de
//    Vectorial (in_S_[1]) -- el unico camino que preserva fidelidad por-lane real
//    en ambos programas (ver handle_config_write). b se escribe en los DOS
//    posibles caminos de entrada -- borde este real de Vectorial (in_E_[1], usado
//    por PROGRAM_VECTOR_ADD) y borde norte real de Enrutamiento (in_N_[0], usado
//    por PROGRAM_FULL_PIPELINE) -- cada programa solo lee el borde que usa.
//  - PROGRAM_SUM_REDUCTION: 8 enteros de 32 bits, no 2 vectores de 4 lanes.
//    word[0] = seed, word[1..7] = los 7 elementos a sumar. Se guardan en
//    sum_reduction_seed_/sum_reduction_vec_ para reproducirlos uno por ciclo
//    recien en START (run_sum_reduction_dataflow) -- no hay borde donde
//    "escribir" 7 valores distintos de una sola vez.
void MeshWrapper::handle_input_write(const unsigned char* data, unsigned int len)
{
    if (len != 32) {
        SC_REPORT_ERROR("MeshWrapper",
            "INPUT_DATA_BUFFER: se esperaban 32 bytes");
        return;
    }

    if (config_ == PROGRAM_SUM_REDUCTION) {
        int32_t words[8];
        std::memcpy(words, data, sizeof(words));
        sum_reduction_seed_ = words[0];
        for (int i = 0; i < SUM_REDUCTION_VECTOR_LEN; ++i) {
            sum_reduction_vec_[i] = words[i + 1];
        }
        // El seed sale por el mismo borde que "b" en PROGRAM_FULL_PIPELINE --
        // run_sum_reduction_dataflow reusa exactamente la misma ruta Enrutamiento
        // -> Memoria -> Enrutamiento -> Escalar.
        Link seed_link;
        for (int lane = 0; lane < 4; ++lane) seed_link[lane] = sum_reduction_seed_;
        in_N_[0].write(seed_link);
        return;
    }

    if (config_ == PROGRAM_SOFTMAX) {
        // Solo word[0..3]: los 4 valores de x en Q4.12. Se guardan para que
        // run_softmax_dataflow los ponga en los bordes recien en START -- las dos
        // reducciones butterfly necesitan reescribir esos bordes varias veces con
        // permutaciones distintas del mismo vector.
        int32_t words[8];
        std::memcpy(words, data, sizeof(words));
        for (int i = 0; i < 4; ++i) {
            softmax_x_[i] = words[i];
        }
        return;
    }

    if (config_ == PROGRAM_EXP_VEC) {
        // Solo word[0..3] se usan: los 4 valores de u en Q4.12, uno por lane.
        // word[4..7] se ignoran (el mapa de registros fija INPUT en 32 bytes, este
        // kernel tiene un solo operando vectorial). Se guardan para que
        // run_exp_vec_dataflow los ponga en el borde sur recien en START, igual que
        // matmul: la saturacion a [EXP_Q_U_MIN, 0] la hace la ALU, no este codigo.
        int32_t words[8];
        std::memcpy(words, data, sizeof(words));
        for (int i = 0; i < 4; ++i) {
            exp_u_[i] = words[i];
        }
        return;
    }

    if (config_ == PROGRAM_MATMUL) {
        // 8 int32 row-major: A = {A00,A01,A10,A11}, B = {B00,B01,B10,B11}. Se
        // guardan y se reordenan por lane recien en run_matmul_dataflow (los
        // bordes reales de Vectorial se manejan alli, no aqui: cada paso k
        // necesita un arreglo por-lane distinto).
        int32_t words[8];
        std::memcpy(words, data, sizeof(words));
        for (int i = 0; i < 4; ++i) {
            matmul_a_[i] = words[i];
            matmul_b_[i] = words[i + 4];
        }
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
