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
//
// pe_mac_program()/pe_mac_clear_acc(): canales laterales de "memoria de
// configuracion", separados del datapath de ejecucion (pe_mac_step). Antes,
// cargar instr_mem competia con el mismo ciclo de ejecucion (pe_mac_step
// aplicaba instr_in despues del fetch) y limpiar acc se hacia pisando
// temporalmente instr_mem con una instruccion MOV_IMM_0->ACC -- eso dejo de
// ser aceptable cuando instr_mem paso a ser persistente entre corridas (ver
// cgra_hls_c/CGRA_Top_C.h): un host que programa la malla una vez y la
// corre muchas veces no puede arriesgarse a que "correr" le pise el
// programa real cada vez que limpia el acumulador.

#ifndef PE_MAC_HLS_C_H
#define PE_MAC_HLS_C_H

#include "../pe_isa_hls_c.h"

// Mismo concepto que PE_SCALAR_NUM_CONTEXTS / PE_VECTOR_NUM_CONTEXTS.
static const int PE_MAC_NUM_CONTEXTS = 4;

template <int DATA_W = 32, int VLEN = 1, int NUM_REGS = 8, int INSTR_MEM_SIZE = 4>
struct PE_MAC_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;

    Instr instr_mem[PE_MAC_NUM_CONTEXTS][INSTR_MEM_SIZE];
    ap_uint<2> active_ctx;

    Link  reg_file[NUM_REGS];
    ap_uint<16> pc;
    Link  acc;

    // Salidas registradas: mismo contrato que un sc_out que solo cambia
    // cuando writeback() lo escribe.
    Link out_N, out_S, out_E, out_W;

    PE_MAC_State() : active_ctx(0), pc(0) {}
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

// Mismo bit-reinterpret que PE_Vector_HLS_C.h (ver el comentario grande en
// pe_isa_hls_c.h): OP_FMUL/OP_FMAC calculan a*b en float32; el += del
// acumulador para OP_FMAC (float, no entero) se resuelve en writeback(),
// igual que OP_MAC ya resolvia el += entero alli mismo (la ALU nunca conoce
// el acumulador).
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
            case OP_MUL:
            case OP_MAC:  r[i] = a[i] * b[i]; break;
            case OP_FMUL:
            case OP_FMAC:
                r[i] = (DATA_W == 32) ? ap_int<DATA_W>(f32_to_bits(f32_from_bits(a[i].to_int()) * f32_from_bits(b[i].to_int()))) : ap_int<DATA_W>(0);
                break;
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
    } else if (ins.opcode == OP_FMAC) {
        // r ya trae a*b en bits float (calculado en alu_compute); el += es
        // en float, no en entero -- reinterpretar+sumar+reempaquetar, igual
        // patron que el resto de OP_F* (ver pe_isa_hls_c.h).
        for (int i = 0; i < VLEN; ++i) {
            float acc_f = f32_from_bits(s.acc[i].to_int());
            float prod_f = f32_from_bits(r[i].to_int());
            s.acc[i] = ap_int<DATA_W>(f32_to_bits(acc_f + prod_f));
        }
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

// Escritura directa a instr_mem: sin fetch/ALU/avance de pc, no compite con
// la ejecucion del datapath. Mismo esquema slot=ctx*INSTR_MEM_SIZE+addr que
// pe_scalar_program/pe_vector_program (ver el comentario grande en
// PE_Scalar_HLS_C.h): programar activa el contexto de inmediato. GEMM 2x2
// (unico consumidor real de esta celda hoy) siempre programa slots 0..3, que
// caen todos en ctx=0 -- comportamiento identico al de antes de este campo.
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_mac_program(PE_MAC_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                            ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    unsigned ctx  = (slot.to_uint() / INSTR_MEM_SIZE) % PE_MAC_NUM_CONTEXTS;
    unsigned addr = slot.to_uint() % INSTR_MEM_SIZE;
    s.instr_mem[ctx][addr] = instr;
    s.active_ctx = ctx;
}

// Reset directo del acumulador, bypass de instr_mem.
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_mac_clear_acc(PE_MAC_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s)
{
    s.acc = PE_VectorData<DATA_W, VLEN>();
}

// Un ciclo de reloj del PE MAC. rst NO limpia acc/instr_mem/reg_file, solo
// pc (mismo precedente que PE_MAC_HLS.h). La carga de programa ya no ocurre
// por este camino (ver pe_mac_program arriba).
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void pe_mac_step(PE_MAC_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                         bool rst, bool enable,
                         const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                         const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    if (rst) {
        s.pc = 0;
        return;
    }
    if (!enable) return;

    PE_Instruction<DATA_W> ins = s.instr_mem[s.active_ctx.to_uint() % PE_MAC_NUM_CONTEXTS][s.pc.to_uint() % INSTR_MEM_SIZE];

    PE_VectorData<DATA_W, VLEN> a = pe_mac_hls_c_detail::select_src(s, ins.src_a, ins.reg_a, ins.imm, in_N, in_S, in_E, in_W);
    PE_VectorData<DATA_W, VLEN> b = pe_mac_hls_c_detail::select_src(s, ins.src_b, ins.reg_b, ins.imm, in_N, in_S, in_E, in_W);
    PE_VectorData<DATA_W, VLEN> r = pe_mac_hls_c_detail::alu_compute(ins.opcode, a, b);

    if (ins.opcode != OP_NOP) pe_mac_hls_c_detail::writeback(s, ins, r);

    s.pc = (s.pc + 1) % INSTR_MEM_SIZE;
}

// Overloads genericos usados por la malla heterogenea (mesh_hls_c/
// CGRA_Mesh_Static_C.h): mismo nombre para las 5 celdas, cada header aporta
// su propio overload -- la malla conoce el tipo concreto de cada celda en
// tiempo de compilacion (parameter pack CellTs...), asi que llamar
// cell_step/cell_program/cell_clear_acc resuelve al overload correcto por
// sobrecarga normal de C++, sin virtuales ni un trait especializado a mano.
template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_step(PE_MAC_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                       bool rst, bool enable,
                       const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                       const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    pe_mac_step(s, rst, enable, in_N, in_S, in_E, in_W);
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_program(PE_MAC_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s,
                          ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    pe_mac_program(s, slot, instr);
}

template <int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void cell_clear_acc(PE_MAC_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& s)
{
    pe_mac_clear_acc(s);
}

#endif // PE_MAC_HLS_C_H
