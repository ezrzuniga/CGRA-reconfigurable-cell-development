// SumReduction16_Spatial_Mesh_C.h
// Mapeo ESPACIAL de una reduccion por suma de 16 elementos sobre la MISMA
// malla 2x2 de 4 celdas que sum_reduction_hls_c/SumReduction_Spatial_Mesh_C.h
// (n=8) -- una malla 2x2 solo tiene 8 puertos externos simultaneos, asi que
// escalar a n=16 con las mismas 4 celdas fisicas exige un diseno de 2
// ETAPAS en vez de "una sola fase, un solo programa":
//
//   Etapa 1 (acumular hojas, 2 fases): cada celda recibe 4 elementos (2 por
//   puerto externo x 2 fases) y los suma en su propio reg0 -- "temporal
//   dentro de cada celda" (reg0 += nuevo par cada fase, auto-referente,
//   protegido por NOP en slots 0/1 igual que MaxReduction_Temporal).
//
//   Etapa 2 (combinar arbol, 1 fase): SE REPROGRAMA la malla (mesh_program,
//   no toca reg_file -- ver comentario de cabecera de CGRA_Top_C.h sobre
//   por que instr_mem y reg_file son canales separados) con un arbol de 3
//   niveles que combina los reg0 YA ACUMULADOS de las 4 celdas (nivel 1 ya
//   no hace falta calcularlo, esta hecho desde la etapa 1) -- mismo patron
//   de relevo que SumReduction_Spatial_Mesh_C.h pero arrancando en el nivel
//   2 en vez del nivel 1.
//
// INSTR_MEM_SIZE es un parametro de PLANTILLA (tamano fijo de instr_mem por
// celda) -- ambas etapas comparten el MISMO tipo de celda, asi que
// necesitan el MISMO INSTR_MEM_SIZE; se usa el mayor de los dos programas
// (7 slots, el que necesita la etapa 2) y la etapa 1 (que solo necesita 4)
// se rellena con NOP hasta 7 -- validado con un modelo Python del FSM
// exacto de cgra_run antes de escribir este header (ver
// sum_reduction16_hls_c/README.md).
//
// Asignacion de elementos v[0..15] (4 por celda, 2 por fase):
//   P00: v0(N,fase0) v1(N,fase1) v2(W,fase0) v3(W,fase1)
//   P01: v4(N,fase0) v5(N,fase1) v6(E,fase0) v7(E,fase1)
//   P10: v8(S,fase0) v9(S,fase1) v10(W,fase0) v11(W,fase1)
//   P11: v12(S,fase0) v13(S,fase1) v14(E,fase0) v15(E,fase1)

#ifndef SUM_REDUCTION16_SPATIAL_MESH_C_H
#define SUM_REDUCTION16_SPATIAL_MESH_C_H

#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int SUMRED16_S_ROWS = 2;
static const int SUMRED16_S_COLS = 2;
static const int SUMRED16_S_DATA_W = 32;
static const int SUMRED16_S_VLEN = 1;
static const int SUMRED16_S_NUM_REGS = 8;
static const int SUMRED16_S_INSTR_MEM_SIZE = 7; // uniforme entre etapa1 y etapa2
static const int SUMRED16_S_STAGE1_NUM_PHASES = 2; // 2 elementos/puerto -> 4/celda
static const int SUMRED16_S_STAGE2_NUM_PHASES = 1; // arbol, un solo pase

typedef PE_Scalar_State<SUMRED16_S_DATA_W, SUMRED16_S_VLEN, SUMRED16_S_NUM_REGS, SUMRED16_S_INSTR_MEM_SIZE>
    SumRed16SpatialCell_C;

typedef CGRA_Mesh_Static_C<SUMRED16_S_ROWS, SUMRED16_S_COLS, SUMRED16_S_DATA_W, SUMRED16_S_VLEN,
                            SumRed16SpatialCell_C, SumRed16SpatialCell_C,
                            SumRed16SpatialCell_C, SumRed16SpatialCell_C>
    SumRed16SpatialMesh_C;
typedef SumRed16SpatialMesh_C::Link  SumRed16SpatialLink_C;
typedef SumRed16SpatialMesh_C::Instr SumRed16SpatialInstr_C;

// ---- Etapa 0: limpiar reg0 (necesaria porque el mesh es `static` y
// persiste entre corridas -- a diferencia de PE_MAC, PE_Scalar no tiene
// mesh_clear_acc() automatico para su reg_file. Sin este paso, una SEGUNDA
// corrida sobre el mismo mesh acumularia sobre el reg0 que dejo la corrida
// anterior en vez de arrancar en 0 -- bug real, encontrado corriendo el
// testbench con 2 casos consecutivos antes de confiar en el diseno).
inline void sumred16_stage0_reset_program_c(
    SumRed16SpatialInstr_C prog[SUMRED16_S_ROWS][SUMRED16_S_COLS][SUMRED16_S_INSTR_MEM_SIZE])
{
    SumRed16SpatialInstr_C zero;
    zero.opcode = OP_MOV; zero.src_a = SRC_IMM; zero.imm = 0; zero.dst = DST_REG; zero.reg_dst = 0;

    for (int r = 0; r < SUMRED16_S_ROWS; r++) {
        for (int c = 0; c < SUMRED16_S_COLS; c++) {
            prog[r][c][0] = SumRed16SpatialInstr_C(); // NOP
            prog[r][c][1] = SumRed16SpatialInstr_C(); // NOP
            prog[r][c][2] = zero;
            for (int s = 3; s < SUMRED16_S_INSTR_MEM_SIZE; s++) prog[r][c][s] = SumRed16SpatialInstr_C();
        }
    }
}

// ---- Etapa 1: acumular 4 elementos propios en reg0 (reejecutado 2 fases) --
// slot0/1=NOP, slot2: temp=portA+portB (reg1), slot3: reg0+=temp, slot4-6:
// NOP de relleno (para igualar INSTR_MEM_SIZE con la etapa 2).
inline void sumred16_stage1_program_c(
    SumRed16SpatialInstr_C prog[SUMRED16_S_ROWS][SUMRED16_S_COLS][SUMRED16_S_INSTR_MEM_SIZE],
    int row, int col, ap_uint<3> port_a, ap_uint<3> port_b)
{
    SumRed16SpatialInstr_C temp, acc;
    temp.opcode = OP_ADD; temp.src_a = port_a; temp.src_b = port_b;
    temp.dst = DST_REG; temp.reg_dst = 1;

    acc.opcode = OP_ADD; acc.src_a = SRC_REG; acc.reg_a = 0; acc.src_b = SRC_REG; acc.reg_b = 1;
    acc.dst = DST_REG; acc.reg_dst = 0;

    prog[row][col][0] = SumRed16SpatialInstr_C(); // NOP (proteccion wrap fantasma)
    prog[row][col][1] = SumRed16SpatialInstr_C(); // NOP
    prog[row][col][2] = temp;
    prog[row][col][3] = acc;
    for (int s = 4; s < SUMRED16_S_INSTR_MEM_SIZE; s++) prog[row][col][s] = SumRed16SpatialInstr_C();
}

inline void sumred16_stage1_program_all_c(
    SumRed16SpatialInstr_C prog[SUMRED16_S_ROWS][SUMRED16_S_COLS][SUMRED16_S_INSTR_MEM_SIZE])
{
    sumred16_stage1_program_c(prog, 0, 0, SRC_NORTH, SRC_WEST); // P00
    sumred16_stage1_program_c(prog, 0, 1, SRC_NORTH, SRC_EAST); // P01
    sumred16_stage1_program_c(prog, 1, 0, SRC_SOUTH, SRC_WEST); // P10
    sumred16_stage1_program_c(prog, 1, 1, SRC_SOUTH, SRC_EAST); // P11
}

// ---- Etapa 2: combinar los reg0 ya acumulados (arbol, nivel 1 ya hecho) --
// P00/P10: relevan reg0 a East de inmediato (nivel1->nivel2).
// P01: combina reg0 con WEST (relevado por P00), releva a South.
// P11: combina reg0 con WEST (relevado por P10), espera, combina con NORTH
//      (relevado por P01), exporta a East -- resultado final.
inline void sumred16_stage2_program_c(
    SumRed16SpatialInstr_C prog[SUMRED16_S_ROWS][SUMRED16_S_COLS][SUMRED16_S_INSTR_MEM_SIZE])
{
    SumRed16SpatialInstr_C nop;
    SumRed16SpatialInstr_C relayE, relayS;
    relayE.opcode = OP_MOV; relayE.src_a = SRC_REG; relayE.reg_a = 0; relayE.dst = DST_EAST;
    relayS.opcode = OP_MOV; relayS.src_a = SRC_REG; relayS.reg_a = 0; relayS.dst = DST_SOUTH;

    // P00 (0,0): slot0/1 NOP, slot2 releva reg0->E, resto NOP.
    prog[0][0][0] = nop; prog[0][0][1] = nop; prog[0][0][2] = relayE;
    for (int s = 3; s < SUMRED16_S_INSTR_MEM_SIZE; s++) prog[0][0][s] = nop;

    // P10 (1,0): igual que P00.
    prog[1][0][0] = nop; prog[1][0][1] = nop; prog[1][0][2] = relayE;
    for (int s = 3; s < SUMRED16_S_INSTR_MEM_SIZE; s++) prog[1][0][s] = nop;

    // P01 (0,1): slot0-2 NOP (P00 releva en su slot2, visible ciclo3),
    // slot3 reg0+=WEST, slot4 releva reg0->S. Resto NOP.
    {
        SumRed16SpatialInstr_C comb;
        comb.opcode = OP_ADD; comb.src_a = SRC_REG; comb.reg_a = 0; comb.src_b = SRC_WEST;
        comb.dst = DST_REG; comb.reg_dst = 0;
        prog[0][1][0] = nop; prog[0][1][1] = nop; prog[0][1][2] = nop;
        prog[0][1][3] = comb;
        prog[0][1][4] = relayS;
        for (int s = 5; s < SUMRED16_S_INSTR_MEM_SIZE; s++) prog[0][1][s] = nop;
    }

    // P11 (1,1): slot0-2 NOP (P10 relay visible ciclo3), slot3 reg0+=WEST,
    // slot4 NOP (margen, espera relevo de P01 visible ciclo5), slot5
    // reg0+=NORTH, slot6 exporta reg0->E (resultado final).
    {
        SumRed16SpatialInstr_C comb1, comb2, exp;
        comb1.opcode = OP_ADD; comb1.src_a = SRC_REG; comb1.reg_a = 0; comb1.src_b = SRC_WEST;
        comb1.dst = DST_REG; comb1.reg_dst = 0;
        comb2.opcode = OP_ADD; comb2.src_a = SRC_REG; comb2.reg_a = 0; comb2.src_b = SRC_NORTH;
        comb2.dst = DST_REG; comb2.reg_dst = 0;
        exp.opcode = OP_MOV; exp.src_a = SRC_REG; exp.reg_a = 0; exp.dst = DST_EAST;

        prog[1][1][0] = nop; prog[1][1][1] = nop; prog[1][1][2] = nop;
        prog[1][1][3] = comb1;
        prog[1][1][4] = nop;
        prog[1][1][5] = comb2;
        prog[1][1][6] = exp;
    }
}

#endif // SUM_REDUCTION16_SPATIAL_MESH_C_H
