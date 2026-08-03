// GEMM_2x2_Mesh.h
// Malla y programa espacial compartidos por GEMM_2x2_MAC__TB.cpp (testbench de
// simulacion, controla la malla via load_instr()/advance_cycles()) y
// GEMM_2x2_HLS_Top.h (wrapper sintetizable, controla la misma malla desde una
// FSM propia) -- una sola fuente de verdad para el typedef de la malla y las 16
// instrucciones del programa, para que no puedan desincronizarse entre los dos.
//
// Programa espacial (una PE_MAC_Cell_HLS por elemento de salida, 4 slots que se
// repiten una vez por cada k=0,1):
//
//        slot0            slot1            slot2            slot3
// P00  MOV W->E(a)     MOV N->S(b)      MAC(W,N)->ACC    MOV ACC->W
// P01  MOV N->S(b)     MAC(W,N)->ACC    NOP              MOV ACC->E
// P10  MOV W->E(a)     NOP              MAC(W,N)->ACC    MOV ACC->W
// P11  NOP             MAC(W,N)->ACC    NOP              MOV ACC->E
//
// A entra por in_W[fila], B por in_N[columna]; C sale por out_W[fila] (col 0)
// y out_E[fila] (col 1).

#ifndef GEMM_2X2_MESH_H
#define GEMM_2X2_MESH_H

#include <systemc.h>
#include "../mesh_hls/CGRA_Mesh_Static.h"
#include "../pe_hls/mac/PE_MAC_Cell_HLS.h"

static const int GEMM_ROWS = 2;
static const int GEMM_COLS = 2;

typedef CGRA_Mesh_Static<GEMM_ROWS, GEMM_COLS, 32, 1,
    PE_MAC_Cell_HLS<32, 1, 8, 4>,
    PE_MAC_Cell_HLS<32, 1, 8, 4>,
    PE_MAC_Cell_HLS<32, 1, 8, 4>,
    PE_MAC_Cell_HLS<32, 1, 8, 4>> GemmMesh;
typedef GemmMesh::Link GemmLink;
typedef GemmMesh::Instr GemmInstr;

static GemmInstr gemm_mac_instr(sc_uint<3> src_a, sc_uint<3> src_b, sc_uint<3> dst) {
    GemmInstr i;
    i.opcode = OP_MAC;
    i.src_a = src_a;
    i.src_b = src_b;
    i.dst = dst;
    return i;
}

static GemmInstr gemm_mov_instr(sc_uint<3> src_a, sc_uint<3> dst) {
    GemmInstr i;
    i.opcode = OP_MOV;
    i.src_a = src_a;
    i.dst = dst;
    return i;
}

static GemmInstr gemm_clear_acc_instr() {
    GemmInstr i;
    i.opcode = OP_MOV;
    i.src_a = SRC_IMM;
    i.imm = 0;
    i.dst = DST_ACC;
    return i;
}

// El programa espacial de GEMM 2x2 descrito en el encabezado.
static void gemm_program(GemmInstr prog[GEMM_ROWS][GEMM_COLS][4]) {
    prog[0][0][0] = gemm_mov_instr(SRC_WEST, DST_EAST);
    prog[0][0][1] = gemm_mov_instr(SRC_NORTH, DST_SOUTH);
    prog[0][0][2] = gemm_mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][0][3] = gemm_mov_instr(SRC_ACC, DST_WEST);

    prog[0][1][0] = gemm_mov_instr(SRC_NORTH, DST_SOUTH);
    prog[0][1][1] = gemm_mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][1][2] = GemmInstr();
    prog[0][1][3] = gemm_mov_instr(SRC_ACC, DST_EAST);

    prog[1][0][0] = gemm_mov_instr(SRC_WEST, DST_EAST);
    prog[1][0][1] = GemmInstr();
    prog[1][0][2] = gemm_mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][0][3] = gemm_mov_instr(SRC_ACC, DST_WEST);

    prog[1][1][0] = GemmInstr();
    prog[1][1][1] = gemm_mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][1][2] = GemmInstr();
    prog[1][1][3] = gemm_mov_instr(SRC_ACC, DST_EAST);
}

// Carga un programa de 4 slots en las 4 PEs, una direccion por ciclo -- llamar
// una vez por ciclo (un advance_cycles(1) de testbench, o un estado de FSM de
// hardware) por cada una de las 4 direcciones.
static void gemm_load_program(GemmMesh& mesh, const GemmInstr prog[GEMM_ROWS][GEMM_COLS][4], int addr) {
    for (int r = 0; r < GEMM_ROWS; r++)
        for (int c = 0; c < GEMM_COLS; c++)
            mesh.load_instr(r, c, addr, prog[r][c][addr]);
}

static void gemm_clear_all_instr(GemmMesh& mesh) {
    for (int r = 0; r < GEMM_ROWS; r++)
        for (int c = 0; c < GEMM_COLS; c++)
            mesh.clear_instr(r, c);
}

// rst no limpia el acumulador de PE_MAC_HLS (mismo precedente que PE_MAC.h) --
// antes de cada corrida hace falta limpiarlo a mano: escribe clear_acc en una
// de las 4 direcciones (llamar una vez por ciclo con addr=0..3); quien la
// invoque debe correr un ciclo mas despues de la ultima llamada para que la
// instruccion realmente se ejecute (ya quedo en las 4 direcciones).
static void gemm_load_clear_acc(GemmMesh& mesh, int addr) {
    GemmInstr clr = gemm_clear_acc_instr();
    for (int r = 0; r < GEMM_ROWS; r++)
        for (int c = 0; c < GEMM_COLS; c++)
            mesh.load_instr(r, c, addr, clr);
}

#endif // GEMM_2X2_MESH_H
