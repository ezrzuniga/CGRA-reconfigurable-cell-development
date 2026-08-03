// GEMM_2x2_Mesh_C.h
// Transliteracion a C/C++ puro de gemm_hls/GEMM_2x2_Mesh.h: mismo typedef de
// malla (2x2, 4 PE_MAC) y mismo programa espacial de 4 slots (una fuente de
// verdad compartida por el testbench), sin SystemC.
//
// GemmMesh_C instancia CGRA_Mesh_Static_C (ya generalizada a heterogenea,
// ver mesh_hls_c/CGRA_Mesh_Static_C.h) con el mismo tipo de celda (PE_MAC)
// repetido 4 veces -- un caso homogeneo es simplemente CellTs... con todos
// los tipos iguales, mismo espiritu que ya probaba mesh_hls/
// CGRA_Mesh_Static.h en el tier SystemC-HLS.
//
// Programa espacial (una celda PE_MAC por elemento de salida, 4 slots que se
// repiten una vez por cada fase k=0,1):
//
//        slot0            slot1            slot2            slot3
// P00  MOV W->E(a)     MOV N->S(b)      MAC(W,N)->ACC    MOV ACC->W
// P01  MOV N->S(b)     MAC(W,N)->ACC    NOP              MOV ACC->E
// P10  MOV W->E(a)     NOP              MAC(W,N)->ACC    MOV ACC->W
// P11  NOP             MAC(W,N)->ACC    NOP              MOV ACC->E
//
// A entra por in_W[fila], B por in_N[columna]; C sale por out_W[fila] (col 0)
// y out_E[fila] (col 1).
//
// gemm_program_c() ya no la carga el top (GEMM_2x2_HLS_Top_C es un wrapper
// delgado sobre cgra_run<...>, ver cgra_hls_c/CGRA_Top_C.h) -- pasa a ser la
// tabla que el testbench usa para generar la secuencia de llamadas con
// prog_valid=true que programan la malla antes de correrla (ver
// GEMM_2x2_HLS_Top_C__TB.cpp).

#ifndef GEMM_2X2_MESH_C_H
#define GEMM_2X2_MESH_C_H

#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int GEMM_ROWS = 2;
static const int GEMM_COLS = 2;
static const int GEMM_DATA_W = 32;
static const int GEMM_VLEN = 1;
static const int GEMM_NUM_REGS = 8;
static const int GEMM_INSTR_MEM_SIZE = 4;
static const int GEMM_NUM_PHASES = 2;

typedef PE_MAC_State<GEMM_DATA_W, GEMM_VLEN, GEMM_NUM_REGS, GEMM_INSTR_MEM_SIZE> GemmCell_C;

typedef CGRA_Mesh_Static_C<GEMM_ROWS, GEMM_COLS, GEMM_DATA_W, GEMM_VLEN,
                            GemmCell_C, GemmCell_C, GemmCell_C, GemmCell_C> GemmMesh_C;
typedef GemmMesh_C::Link  GemmLink_C;
typedef GemmMesh_C::Instr GemmInstr_C;

inline GemmInstr_C gemm_mac_instr_c(ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<3> dst) {
    GemmInstr_C i;
    i.opcode = OP_MAC;
    i.src_a = src_a;
    i.src_b = src_b;
    i.dst = dst;
    return i;
}

inline GemmInstr_C gemm_mov_instr_c(ap_uint<3> src_a, ap_uint<3> dst) {
    GemmInstr_C i;
    i.opcode = OP_MOV;
    i.src_a = src_a;
    i.dst = dst;
    return i;
}

inline GemmInstr_C gemm_clear_acc_instr_c() {
    GemmInstr_C i;
    i.opcode = OP_MOV;
    i.src_a = SRC_IMM;
    i.imm = 0;
    i.dst = DST_ACC;
    return i;
}

// El programa espacial de GEMM 2x2 descrito en el encabezado.
inline void gemm_program_c(GemmInstr_C prog[GEMM_ROWS][GEMM_COLS][GEMM_INSTR_MEM_SIZE]) {
    prog[0][0][0] = gemm_mov_instr_c(SRC_WEST, DST_EAST);
    prog[0][0][1] = gemm_mov_instr_c(SRC_NORTH, DST_SOUTH);
    prog[0][0][2] = gemm_mac_instr_c(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][0][3] = gemm_mov_instr_c(SRC_ACC, DST_WEST);

    prog[0][1][0] = gemm_mov_instr_c(SRC_NORTH, DST_SOUTH);
    prog[0][1][1] = gemm_mac_instr_c(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][1][2] = GemmInstr_C();
    prog[0][1][3] = gemm_mov_instr_c(SRC_ACC, DST_EAST);

    prog[1][0][0] = gemm_mov_instr_c(SRC_WEST, DST_EAST);
    prog[1][0][1] = GemmInstr_C();
    prog[1][0][2] = gemm_mac_instr_c(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][0][3] = gemm_mov_instr_c(SRC_ACC, DST_WEST);

    prog[1][1][0] = GemmInstr_C();
    prog[1][1][1] = gemm_mac_instr_c(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][1][2] = GemmInstr_C();
    prog[1][1][3] = gemm_mov_instr_c(SRC_ACC, DST_EAST);
}

#endif // GEMM_2X2_MESH_C_H
