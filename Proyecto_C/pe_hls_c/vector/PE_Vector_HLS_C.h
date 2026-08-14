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

template <int DATA_W = 32, int VLEN = 4, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
struct PE_Vector_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;

    Instr instr_mem[INSTR_MEM_SIZE];
    Link  reg_file[NUM_REGS];
    ap_uint<16> pc;

    Link out_N, out_S, out_E, out_W;

    // Memorias locales del PE particionadas por completo -- ver la
    // justificacion en PE_MAC_HLS_C.h (fetch + 2 operandos + writeback en el
    // mismo ciclo; como RAM inferida el PE no cerraria II=1). ARRAY_PARTITION
    // en el cuerpo del constructor, no en el cuerpo del struct: Vitis HLS
    // 2024.1 rechaza `#pragma HLS` fuera de function scope.
    PE_Vector_State() : pc(0) {
#pragma HLS ARRAY_PARTITION variable=instr_mem complete dim=1
#pragma HLS ARRAY_PARTITION variable=reg_file complete dim=1
    }
};

namespace pe_vector_hls_c_detail {

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline PE_VectorData<DATA_W, VLEN> select_src(
    const PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s, ap_uint<3> sel,
    ap_uint<5> reg_idx, ap_int<DATA_W> imm,
    const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
    const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
#pragma HLS INLINE
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

template <int DATA_W, int VLEN>
inline PE_VectorData<DATA_W, VLEN> alu_compute(
    ap_uint<4> opcode, const PE_VectorData<DATA_W, VLEN>& a, const PE_VectorData<DATA_W, VLEN>& b)
{
#pragma HLS INLINE
    PE_VectorData<DATA_W, VLEN> r;
    // UNROLL: una ALU fisica por lane -- el paralelismo SIMD del PE vectorial
    // (ver PE_MAC_HLS_C.h).
alu_lane_loop:
    for (int i = 0; i < VLEN; ++i) {
#pragma HLS UNROLL
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
            default:      r[i] = 0; break;
        }
    }
    return r;
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void writeback(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                       const PE_Instruction<DATA_W>& ins, const PE_VectorData<DATA_W, VLEN>& r)
{
#pragma HLS INLINE
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

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_vector_program(PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                               ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    s.instr_mem[slot.to_uint() % INSTR_MEM_SIZE] = instr;
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
    // INLINE sin PIPELINE propio -- ver PE_MAC_HLS_C.h (Vitis HLS 2024.1
    // rechaza combinar ambos pragmas en la misma funcion; el II=1 real lo
    // aporta el PIPELINE de mesh_step()).
#pragma HLS INLINE
    if (rst) {
        s.pc = 0;
        return;
    }
    if (!enable) return;

    PE_Instruction<DATA_W> ins = s.instr_mem[s.pc.to_uint() % INSTR_MEM_SIZE];

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
