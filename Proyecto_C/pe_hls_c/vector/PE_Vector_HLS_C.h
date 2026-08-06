// PE_Vector_HLS_C.h
// Transliteracion a C/C++ puro de pe/vector/PE_vector.h + PE_Vector_Cell.h.
// Practicamente una copia de PE_MAC_HLS_C.h sin acumulador (sin
// OP_MAC/SRC_ACC/DST_ACC) -- el puerto de menor riesgo de los 4 nuevos,
// porque PE_vector ya hablaba nativamente en PE_VectorData<DATA_W,VLEN> (el
// mismo Link de la malla), sin necesitar puente lane0/broadcast como
// PE_scalar.
//
// Se elimina PE_VecInstruction/PE_VecInstrIn (existian en pe_isa.h solo por
// el choque de nombres de template en C++ clasico) -- se reusa el
// PE_Instruction/PE_InstrIn canonico para instr_mem/programacion, mismo
// precedente ya aplicado al portar MAC (VLEN nunca aparecio realmente en el
// tipo de instruccion).
//
// select_src(SRC_IMM) difunde el inmediato escalar a los VLEN lanes -- el
// unico lugar donde "vector" agrega logica real mas alla de sustituir tipos
// (igual que en el PE_vector original).

#ifndef PE_VECTOR_HLS_C_H
#define PE_VECTOR_HLS_C_H

#include "../pe_isa_hls_c.h"

// Mismo concepto que PE_SCALAR_NUM_CONTEXTS / RC_NUM_CONTEXTS.
static const int PE_VECTOR_NUM_CONTEXTS = 4;

template <int DATA_W = 32, int VLEN = 8, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
struct PE_Vector_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;

    Instr instr_mem[PE_VECTOR_NUM_CONTEXTS][INSTR_MEM_SIZE];
    ap_uint<2> active_ctx;

    Link  reg_file[NUM_REGS];  // NUM_REGS(8) x VLEN(8) x DATA_W(32) = 8 x 256b con los defaults
    ap_uint<16> pc;

    Link out_N, out_S, out_E, out_W;

    PE_Vector_State() : active_ctx(0), pc(0) {}
};

namespace pe_vector_hls_c_detail {

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline PE_VectorData<DATA_W, VLEN> select_src(
    const PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s, ap_uint<3> sel,
    ap_uint<5> reg_idx, ap_int<DATA_W> imm,
    const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
    const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    switch (sel) {
        case SRC_REG:   return s.reg_file[reg_idx.to_uint() % NUM_REGS];
        case SRC_NORTH: return in_N;
        case SRC_SOUTH: return in_S;
        case SRC_EAST:  return in_E;
        case SRC_WEST:  return in_W;
        case SRC_IMM: {
            PE_VectorData<DATA_W, VLEN> v;
            for (int i = 0; i < VLEN; ++i) v[i] = imm;
            return v;
        }
        default: return PE_VectorData<DATA_W, VLEN>();
    }
}

// Opcodes OP_F* (ver pe_isa_hls_c.h): cada lane se reinterpreta como
// IEEE-754 float32 (f32_from_bits/f32_to_bits, bit_cast puro, no
// conversion numerica), se opera en float, y el resultado se vuelve a
// empaquetar como el mismo patron de bits en el lane ap_int<DATA_W> de
// salida -- el wire de la malla sigue siendo entero (ver el comentario
// grande en pe_isa_hls_c.h). Solo tiene efecto real con DATA_W=32.
template <int DATA_W, int VLEN>
inline PE_VectorData<DATA_W, VLEN> alu_compute(
    ap_uint<5> opcode, const PE_VectorData<DATA_W, VLEN>& a, const PE_VectorData<DATA_W, VLEN>& b)
{
    PE_VectorData<DATA_W, VLEN> r;
    for (int i = 0; i < VLEN; ++i) {
        unsigned shamt = b[i].to_uint() & (DATA_W - 1);
        switch (opcode) {
            case OP_ADD:  r[i] = a[i] + b[i]; break;
            case OP_SUB:  r[i] = a[i] - b[i]; break;
            case OP_AND:  r[i] = a[i] & b[i]; break;
            case OP_OR:   r[i] = a[i] | b[i]; break;
            case OP_XOR:  r[i] = a[i] ^ b[i]; break;
            case OP_MOV:  r[i] = a[i]; break;
            case OP_SLL:  r[i] = a[i] << shamt; break;
            case OP_SRL:  r[i] = ap_int<DATA_W>(ap_uint<DATA_W>(a[i]) >> shamt); break;
            case OP_SRA:  r[i] = a[i] >> shamt; break;
            case OP_SLT:  r[i] = (a[i] < b[i]) ? 1 : 0; break;
            case OP_SLTU: r[i] = (ap_uint<DATA_W>(a[i]) < ap_uint<DATA_W>(b[i])) ? 1 : 0; break;
            case OP_MUL:  r[i] = a[i] * b[i]; break;
            case OP_FADD:
                r[i] = (DATA_W == 32) ? ap_int<DATA_W>(f32_to_bits(f32_from_bits(a[i].to_int()) + f32_from_bits(b[i].to_int()))) : ap_int<DATA_W>(0);
                break;
            case OP_FSUB:
                r[i] = (DATA_W == 32) ? ap_int<DATA_W>(f32_to_bits(f32_from_bits(a[i].to_int()) - f32_from_bits(b[i].to_int()))) : ap_int<DATA_W>(0);
                break;
            case OP_FMUL:
                r[i] = (DATA_W == 32) ? ap_int<DATA_W>(f32_to_bits(f32_from_bits(a[i].to_int()) * f32_from_bits(b[i].to_int()))) : ap_int<DATA_W>(0);
                break;
            default:      r[i] = 0; break;
        }
    }
    return r;
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void writeback(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                       const PE_Instruction<DATA_W>& ins, const PE_VectorData<DATA_W, VLEN>& r)
{
    switch (ins.dst) {
        case DST_REG:
            s.reg_file[ins.reg_dst.to_uint() % NUM_REGS] = r;
            break;
        case DST_NORTH: s.out_N = r; break;
        case DST_SOUTH: s.out_S = r; break;
        case DST_EAST:  s.out_E = r; break;
        case DST_WEST:  s.out_W = r; break;
        case DST_ALL:
            s.out_N = r; s.out_S = r; s.out_E = r; s.out_W = r;
            break;
        default: break;
    }
}

} // namespace pe_vector_hls_c_detail

// Mismo esquema slot=ctx*INSTR_MEM_SIZE+addr que pe_scalar_program (ver el
// comentario grande alli): programar activa el contexto de inmediato.
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_vector_program(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                               ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    unsigned ctx  = (slot.to_uint() / INSTR_MEM_SIZE) % PE_VECTOR_NUM_CONTEXTS;
    unsigned addr = slot.to_uint() % INSTR_MEM_SIZE;
    s.instr_mem[ctx][addr] = instr;
    s.active_ctx = ctx;
}

// PE_vector no tiene acumulador -- no-op (ver PE_Scalar_HLS_C.h).
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_vector_clear_acc(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>&) {}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_vector_step(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                            bool rst, bool enable,
                            const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                            const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    if (rst) {
        s.pc = 0;
        return;
    }
    if (!enable) return;

    PE_Instruction<DATA_W> ins = s.instr_mem[s.active_ctx.to_uint() % PE_VECTOR_NUM_CONTEXTS][s.pc.to_uint() % INSTR_MEM_SIZE];

    PE_VectorData<DATA_W, VLEN> a = pe_vector_hls_c_detail::select_src(s, ins.src_a, ins.reg_a, ins.imm, in_N, in_S, in_E, in_W);
    PE_VectorData<DATA_W, VLEN> b = pe_vector_hls_c_detail::select_src(s, ins.src_b, ins.reg_b, ins.imm, in_N, in_S, in_E, in_W);
    PE_VectorData<DATA_W, VLEN> r = pe_vector_hls_c_detail::alu_compute(ins.opcode, a, b);

    if (ins.opcode != OP_NOP) pe_vector_hls_c_detail::writeback(s, ins, r);

    s.pc = (s.pc + 1) % INSTR_MEM_SIZE;
}

// Overloads genericos para el dispatch de la malla heterogenea.
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_step(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                       bool rst, bool enable,
                       const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                       const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    pe_vector_step(s, rst, enable, in_N, in_S, in_E, in_W);
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_program(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                          ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    pe_vector_program(s, slot, instr);
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_clear_acc(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s)
{
    pe_vector_clear_acc(s);
}

#endif // PE_VECTOR_HLS_C_H
