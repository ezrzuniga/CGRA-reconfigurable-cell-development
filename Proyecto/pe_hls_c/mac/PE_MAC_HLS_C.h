// PE_MAC_HLS_C.h
// Transliteracion a C/C++ puro (sin sc_module/puertos/sc_signal) de
// pe_hls/mac/PE_MAC_HLS.h, para Vitis HLS 2024.1 (que rechaza SystemC como
// top de sintesis, ver Proyecto_HLS/hls_vitis_gemm_2x2_cgra/vitis_hls.log).
// Mismo datapath, mismo ISA, misma semantica de acumulador (rst NO limpia
// acc, solo DST_ACC lo limpia/precarga) -- unica diferencia real es que
// out_N/S/E/W pasan de sc_out (puerto) a campos persistentes de
// PE_MAC_State: siguen siendo "registros" que retienen su ultimo valor
// cuando la instruccion de un ciclo no los toca, exactamente igual que un
// sc_out que no recibe write() ese ciclo.

#ifndef PE_MAC_HLS_C_H
#define PE_MAC_HLS_C_H

#include "../pe_isa_hls_c.h"

template <int DATA_W = 32, int VLEN = 1, int NUM_REGS = 8, int INSTR_MEM_SIZE = 4>
struct PE_MAC_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;

    Instr instr_mem[INSTR_MEM_SIZE];
    Link  reg_file[NUM_REGS];
    ap_uint<16> pc;
    Link  acc;

    // Salidas registradas: mismo contrato que un sc_out que solo cambia
    // cuando writeback() lo escribe.
    Link out_N, out_S, out_E, out_W;

    PE_MAC_State() : pc(0) {}
};

namespace pe_mac_hls_c_detail {

template <int DATA_W, int VLEN, int NUM_REGS>
inline PE_VectorData<DATA_W, VLEN> select_src(
    const PE_MAC_State<DATA_W, VLEN, NUM_REGS>& s, ap_uint<3> sel,
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
        case SRC_ACC:   return s.acc;
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
            case OP_MUL:
            case OP_MAC:  r[i] = a[i] * b[i]; break;
            default:      r[i] = 0; break;
        }
    }
    return r;
}

template <int DATA_W, int VLEN, int NUM_REGS>
inline void writeback(PE_MAC_State<DATA_W, VLEN, NUM_REGS>& s,
                       const PE_Instruction<DATA_W>& ins, PE_VectorData<DATA_W, VLEN> r)
{
    if (ins.opcode == OP_MAC) {
        for (int i = 0; i < VLEN; ++i) s.acc[i] = s.acc[i] + r[i];
        r = s.acc;
    } else if (ins.dst == DST_ACC) {
        s.acc = r;
    }

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
        case DST_ACC: break; // ya resuelto arriba
        default: break;
    }
}

} // namespace pe_mac_hls_c_detail

// Un ciclo de reloj del PE MAC. rst NO limpia acc/instr_mem/reg_file, solo
// pc (mismo precedente que PE_MAC_HLS.h). instr_in llega ya resuelto para
// este ciclo (quien orquesta la malla decide addr/instr/valid).
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_mac_step(PE_MAC_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                         bool rst, bool enable,
                         const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                         const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W,
                         const PE_InstrIn<DATA_W>& instr_in)
{
    if (rst) {
        s.pc = 0;
        return;
    }
    if (!enable) return;

    // Fetch antes de aplicar una carga pendiente de este mismo ciclo
    // (semantica read-old-data de BRAM de un puerto, igual que PE_MAC_HLS.h).
    PE_Instruction<DATA_W> ins = s.instr_mem[s.pc.to_uint() % INSTR_MEM_SIZE];

    if (instr_in.valid) {
        s.instr_mem[instr_in.addr.to_uint() % INSTR_MEM_SIZE] = instr_in.instr;
    }

    PE_VectorData<DATA_W, VLEN> a = pe_mac_hls_c_detail::select_src(s, ins.src_a, ins.reg_a, ins.imm, in_N, in_S, in_E, in_W);
    PE_VectorData<DATA_W, VLEN> b = pe_mac_hls_c_detail::select_src(s, ins.src_b, ins.reg_b, ins.imm, in_N, in_S, in_E, in_W);
    PE_VectorData<DATA_W, VLEN> r = pe_mac_hls_c_detail::alu_compute(ins.opcode, a, b);

    if (ins.opcode != OP_NOP) pe_mac_hls_c_detail::writeback(s, ins, r);

    s.pc = (s.pc + 1) % INSTR_MEM_SIZE;
}

#endif // PE_MAC_HLS_C_H
