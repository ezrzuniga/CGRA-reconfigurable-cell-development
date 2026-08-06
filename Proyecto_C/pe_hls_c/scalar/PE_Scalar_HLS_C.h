// PE_Scalar_HLS_C.h
// Transliteracion a C/C++ puro de pe/scalar/PE_scalar.h + PE_Scalar_Cell.h
// (ver PE_MAC_HLS_C.h para el mismo patron general ya aplicado a MAC).
//
// reg_file/pc/instr_mem quedan escalares (ap_int<DATA_W>), igual que el
// PE_scalar real -- el registro de esta PE nunca fue vectorial. Las salidas
// registradas (out_N/S/E/W) SI quedan tipadas Link=PE_VectorData<DATA_W,VLEN>
// para que la malla heterogenea pueda cablear esta celda igual que cualquier
// otra: pe_scalar_step() lee el lane 0 de cada entrada (equivalente a los 4
// bridge_in_* de PE_Scalar_Cell) y hace broadcast del resultado escalar a
// todos los lanes al escribir una salida (equivalente a los 4 bridge_out_*)
// -- plegado dentro de la misma funcion, sin un modulo/wrapper aparte, porque
// en C no hay jerarquia de puertos que puentear.
//
// Se elimina por completo el mecanismo valid_toggle/writeback-sensible-al-
// toggle de ALU_scalar.h/PE_scalar.h: es un artefacto exclusivo de la
// simulacion por delta-ciclo de SystemC (sc_signal::write() no dispara
// evento si el valor nuevo es igual al viejo, asi que un acumulador que
// repite resultado podia perder la escritura). En C, writeback() se llama
// simplemente cada ciclo habilitado, en orden de programa -- no hace falta
// forzar ningun disparo.

#ifndef PE_SCALAR_HLS_C_H
#define PE_SCALAR_HLS_C_H

#include "../pe_isa_hls_c.h"

// Numero de contextos de programa por celda -- mismo concepto y mismo valor
// que RC_NUM_CONTEXTS en Routing_Cell_HLS_C.h / los 4 contextos de
// PE_Memory_HLS_C.h: hasta 4 programas pre-cargados, uno activo a la vez
// (pe_scalar_program() activa el contexto que programa, igual filosofia que
// routing_cell_program()).
static const int PE_SCALAR_NUM_CONTEXTS = 4;

template <int DATA_W = 32, int VLEN = 1, int NUM_REGS = 16, int INSTR_MEM_SIZE = 16>
struct PE_Scalar_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;

    Instr instr_mem[PE_SCALAR_NUM_CONTEXTS][INSTR_MEM_SIZE];
    ap_uint<2> active_ctx;

    ap_int<DATA_W> reg_file[NUM_REGS];
    ap_uint<16> pc;

    // Bucle HW: OP_LOOP fija loop_start (la instruccion siguiente) y
    // loop_iters (cuantas veces mas debe repetirse); OP_ENDLOOP decrementa
    // loop_iters y salta pc de vuelta a loop_start mientras queden
    // iteraciones, sin recargar instr_mem ni gastar un ciclo de mas en el
    // salto (se resuelve en el mismo ciclo que OP_ENDLOOP).
    ap_uint<16> loop_start;
    ap_uint<16> loop_iters;

    // Predicado de 1 bit: OP_PSET lo actualiza; cualquier instruccion con
    // pred_gate=1 solo hace writeback si pred==true (ver pe_scalar_step()).
    bool pred;

    // Salidas registradas: cada una es el ultimo escalar escrito, difundido a
    // los VLEN lanes del wire de la malla.
    Link out_N, out_S, out_E, out_W;

    PE_Scalar_State() : active_ctx(0), pc(0), loop_start(0), loop_iters(0), pred(false) {
        // reg_file no tenia inicializacion explicita antes de este fix --
        // dependia de que ap_int<DATA_W> por defecto valga 0, que no es
        // garantia real (quedaba en lo que hubiera en esa memoria). Lo
        // expuso el primer test que hace read-modify-write sobre un
        // registro sin escribirlo antes (el bucle HW: reg2 = reg2 + 1).
        for (int i = 0; i < NUM_REGS; i++) reg_file[i] = 0;
    }
};

namespace pe_scalar_hls_c_detail {

template <int DATA_W, int VLEN>
inline PE_VectorData<DATA_W, VLEN> broadcast(ap_int<DATA_W> v) {
    PE_VectorData<DATA_W, VLEN> r;
    for (int i = 0; i < VLEN; ++i) r[i] = v;
    return r;
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline ap_int<DATA_W> select_src(
    const PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s, ap_uint<3> sel,
    ap_uint<5> reg_idx, ap_int<DATA_W> imm,
    ap_int<DATA_W> in_N, ap_int<DATA_W> in_S, ap_int<DATA_W> in_E, ap_int<DATA_W> in_W)
{
    switch (sel) {
        case SRC_REG:   return s.reg_file[reg_idx.to_uint() % NUM_REGS];
        case SRC_NORTH: return in_N;
        case SRC_SOUTH: return in_S;
        case SRC_EAST:  return in_E;
        case SRC_WEST:  return in_W;
        case SRC_IMM:   return imm;
        default:        return ap_int<DATA_W>(0);
    }
}

template <int DATA_W>
inline ap_int<DATA_W> alu_compute(ap_uint<5> opcode, ap_int<DATA_W> a, ap_int<DATA_W> b) {
    unsigned shamt = b.to_uint() & (DATA_W - 1);
    switch (opcode) {
        case OP_ADD:  return a + b;
        case OP_SUB:  return a - b;
        case OP_AND:  return a & b;
        case OP_OR:   return a | b;
        case OP_XOR:  return a ^ b;
        case OP_MOV:  return a;
        case OP_SLL:  return a << shamt;
        case OP_SRL:  return ap_int<DATA_W>(ap_uint<DATA_W>(a) >> shamt);
        case OP_SRA:  return a >> shamt;
        case OP_SLT:  return (a < b) ? ap_int<DATA_W>(1) : ap_int<DATA_W>(0);
        case OP_SLTU: return (ap_uint<DATA_W>(a) < ap_uint<DATA_W>(b)) ? ap_int<DATA_W>(1) : ap_int<DATA_W>(0);
        case OP_MUL:  return a * b;
        default:      return ap_int<DATA_W>(0);
    }
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void writeback(PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                       const PE_Instruction<DATA_W>& ins, ap_int<DATA_W> r)
{
    switch (ins.dst) {
        case DST_REG:
            s.reg_file[ins.reg_dst.to_uint() % NUM_REGS] = r;
            break;
        case DST_NORTH: s.out_N = broadcast<DATA_W, VLEN>(r); break;
        case DST_SOUTH: s.out_S = broadcast<DATA_W, VLEN>(r); break;
        case DST_EAST:  s.out_E = broadcast<DATA_W, VLEN>(r); break;
        case DST_WEST:  s.out_W = broadcast<DATA_W, VLEN>(r); break;
        case DST_ALL: {
            PE_VectorData<DATA_W, VLEN> v = broadcast<DATA_W, VLEN>(r);
            s.out_N = v; s.out_S = v; s.out_E = v; s.out_W = v;
            break;
        }
        default: break;
    }
}

} // namespace pe_scalar_hls_c_detail

// Escritura directa a instr_mem -- canal lateral, igual convencion que
// pe_mac_program (PE_MAC_HLS_C.h). `slot` sigue siendo el mismo parametro
// generico de <tipo>_program en las 5 celdas (ver project.md, "slot es
// deliberadamente polimorfico"): aca codifica contexto Y direccion en uno
// solo, ctx = slot / INSTR_MEM_SIZE, addr = slot % INSTR_MEM_SIZE -- mismo
// truco que ya usa routing_cell_program con RC_NUM_CONTEXTS, sin necesitar
// cambiar la firma compartida por las 5 celdas. Programar un slot de un
// contexto TAMBIEN activa ese contexto de inmediato (mismo comportamiento
// que routing_cell_program).
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_scalar_program(PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                               ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    unsigned ctx  = (slot.to_uint() / INSTR_MEM_SIZE) % PE_SCALAR_NUM_CONTEXTS;
    unsigned addr = slot.to_uint() % INSTR_MEM_SIZE;
    s.instr_mem[ctx][addr] = instr;
    s.active_ctx = ctx;
}

// PE_scalar no tiene acumulador -- no-op. Existe para que la malla
// heterogenea pueda "limpiar todos los acumuladores" de forma uniforme sin
// conocer el tipo concreto de cada celda.
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_scalar_clear_acc(PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>&) {}

// Un ciclo de reloj de la PE escalar. rst NO limpia reg_file/instr_mem, solo
// pc (mismo precedente que pe_mac_step) -- tampoco limpia loop_iters/pred:
// mismo precedente que acc en PE_MAC (solo una instruccion explicita, aca
// OP_LOOP/OP_PSET, los reinicia).
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_scalar_step(PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                            bool rst, bool enable,
                            const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                            const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    if (rst) {
        s.pc = 0;
        return;
    }
    if (!enable) return;

    PE_Instruction<DATA_W> ins = s.instr_mem[s.active_ctx.to_uint() % PE_SCALAR_NUM_CONTEXTS][s.pc.to_uint() % INSTR_MEM_SIZE];

    // Puente lane0-en-lectura: la malla habla en Link, esta celda opera
    // escalar (equivalente a los 4 bridge_in_* de PE_Scalar_Cell).
    ap_int<DATA_W> sa_N = in_N[0], sa_S = in_S[0], sa_E = in_E[0], sa_W = in_W[0];

    ap_int<DATA_W> a = pe_scalar_hls_c_detail::select_src(s, ins.src_a, ins.reg_a, ins.imm, sa_N, sa_S, sa_E, sa_W);
    ap_int<DATA_W> b = pe_scalar_hls_c_detail::select_src(s, ins.src_b, ins.reg_b, ins.imm, sa_N, sa_S, sa_E, sa_W);

    // Bucle HW y predicado tienen precedencia sobre el datapath ALU normal
    // -- son instrucciones de control de la propia celda, no producen un
    // resultado que vaya a reg_file/malla.
    ap_uint<16> next_pc = (s.pc + 1) % INSTR_MEM_SIZE;

    if (ins.opcode == OP_LOOP) {
        s.loop_start = next_pc;              // el cuerpo del bucle empieza en la siguiente instruccion
        s.loop_iters = ap_uint<16>(b.to_uint());  // trip count en el operando b
    } else if (ins.opcode == OP_ENDLOOP) {
        if (s.loop_iters > 1) {
            s.loop_iters = s.loop_iters - 1;
            next_pc = s.loop_start;          // salto de vuelta al inicio del cuerpo, costo cero
        } else {
            s.loop_iters = 0;
        }
    } else if (ins.opcode == OP_PSET) {
        s.pred = (a.to_int() != 0);
    } else {
        ap_int<DATA_W> r = pe_scalar_hls_c_detail::alu_compute<DATA_W>(ins.opcode, a, b);
        if (ins.opcode != OP_NOP && (ins.pred_gate == 0 || s.pred)) {
            pe_scalar_hls_c_detail::writeback(s, ins, r);
        }
    }

    s.pc = next_pc;
}

// Overloads genericos para el dispatch de la malla heterogenea (ver
// PE_MAC_HLS_C.h para la explicacion del mecanismo).
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_step(PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                       bool rst, bool enable,
                       const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                       const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    pe_scalar_step(s, rst, enable, in_N, in_S, in_E, in_W);
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_program(PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                          ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    pe_scalar_program(s, slot, instr);
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_clear_acc(PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s)
{
    pe_scalar_clear_acc(s);
}

#endif // PE_SCALAR_HLS_C_H
