// pe_isa_hls_c.h
// ISA compartida por la variante C/C++ pura del PE MAC, transliterada de
// pe/pe_isa.h: mismos opcodes/campos/anchos de bit, pero sobre ap_int/ap_uint
// (tipo nativo de Vitis HLS, sin dependencia de libSystemC) en vez de
// sc_int/sc_uint, y sin los overloads de operator<</sc_trace (no aplican
// fuera de un modulo SystemC).

#ifndef PE_ISA_HLS_C_H
#define PE_ISA_HLS_C_H

#include <ap_int.h>

enum PE_Opcode {
    OP_NOP  = 0,
    OP_ADD  = 1,
    OP_SUB  = 2,
    OP_AND  = 3,
    OP_OR   = 4,
    OP_XOR  = 5,
    OP_MOV  = 6,
    OP_SLL  = 7,
    OP_SRL  = 8,
    OP_SRA  = 9,
    OP_SLT  = 10,
    OP_SLTU = 11,
    OP_MUL  = 12,
    OP_MAC  = 13
};

enum PE_Src {
    SRC_REG   = 0,
    SRC_NORTH = 1,
    SRC_SOUTH = 2,
    SRC_EAST  = 3,
    SRC_WEST  = 4,
    SRC_IMM   = 5,
    SRC_ACC   = 6
};

enum PE_Dst {
    DST_REG   = 0,
    DST_NORTH = 1,
    DST_SOUTH = 2,
    DST_EAST  = 3,
    DST_WEST  = 4,
    DST_ALL   = 5,
    DST_ACC   = 6
};

template <int DATA_W = 32>
struct PE_Instruction {
    ap_uint<4> opcode;
    ap_uint<3> src_a;
    ap_uint<3> src_b;
    ap_uint<3> dst;
    ap_uint<5> reg_a;
    ap_uint<5> reg_b;
    ap_uint<5> reg_dst;
    ap_int<DATA_W> imm;

    PE_Instruction()
        : opcode(OP_NOP), src_a(SRC_REG), src_b(SRC_REG), dst(DST_REG),
          reg_a(0), reg_b(0), reg_dst(0), imm(0) {}
};

template <int DATA_W = 32>
struct PE_InstrIn {
    bool valid;
    ap_uint<8> addr;
    PE_Instruction<DATA_W> instr;

    PE_InstrIn() : valid(false), addr(0), instr() {}
};

// Dato vectorial: VLEN lanes independientes de ap_int<DATA_W>. Wire unico de
// la malla (GEMM 2x2 solo usa VLEN=1, pero se mantiene generico por
// consistencia con pe_isa.h).
//
// ARRAY_PARTITION complete sobre `lane`: es EL pragma que decide si la CGRA
// tiene paralelismo de lanes o no. Sin el, Vitis HLS puede mapear los VLEN
// lanes a una RAM de 1-2 puertos y la ALU SIMD de cada PE se serializa a
// 1 lane por ciclo (VLEN=4 -> 4x mas latencia por instruccion). Particionado
// completo, los VLEN lanes son registros independientes y el bucle por lane de
// alu_compute() se desenrolla a VLEN ALUs fisicas en paralelo -- que es
// exactamente el modelo de un PE vectorial de CGRA. Va dentro del struct para
// que aplique a TODAS las instancias de Link (puertos, registros, snapshots de
// la malla), no solo a una declaracion suelta.
template <int DATA_W = 32, int VLEN = 4>
struct PE_VectorData {
    ap_int<DATA_W> lane[VLEN];
#pragma HLS ARRAY_PARTITION variable=lane complete dim=1

    PE_VectorData() {
        for (int i = 0; i < VLEN; i++) {
#pragma HLS UNROLL
            lane[i] = 0;
        }
    }

    ap_int<DATA_W>& operator[](int idx) { return lane[idx]; }
    const ap_int<DATA_W>& operator[](int idx) const { return lane[idx]; }
};

#endif // PE_ISA_HLS_C_H
