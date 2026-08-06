// SumReduction_Spatial_Mesh_C.h
// Mapeo ESPACIAL de la reduccion por suma sobre el template generico
// cgra_run<...>: 4 celdas PE_Scalar en una malla 2x2 (mismo layout fisico
// que gemm_hls_c/GEMM_2x2_Mesh_C.h, pero con 4 PE_Scalar en vez de 4
// PE_MAC), formando un arbol de sumas de profundidad 3 en vez de una cadena
// de 8 acumulaciones secuenciales sobre 1 sola celda (contraparte:
// SumReduction_Temporal_Mesh_C.h). NUM_PHASES=1 -- los 8 elementos entran
// TODOS en el mismo instante (una sola fase), uno por cada uno de los 8
// puertos externos que tiene un mesh 2x2 (cada celda de un 2x2 esta en el
// borde por las 2 dimensiones, asi que cada una expone 2 puertos externos:
// ver mesh_hls_c/CGRA_Mesh_Static_C.h::step_one, r==0/r==ROWS-1 y
// c==0/c==COLS-1 se solapan cuando ROWS=COLS=2).
//
// Asignacion de entradas (v0..v7), una por puerto externo:
//   P00.N=v0  P00.W=v1        P01.N=v2  P01.E=v3
//   P10.S=v4  P10.W=v5        P11.S=v6  P11.E=v7
//
// Programa espacial (3 slots, un nivel de arbol por slot; NOP implicito en
// las celdas que ya terminaron su trabajo):
//
//        slot0                  slot1                   slot2
// P00  ADD(N,W)->EAST        NOP                      NOP
// P01  ADD(N,E)->REG0        ADD(REG0,WEST)->SOUTH    NOP
// P10  ADD(S,W)->EAST        NOP                      NOP
// P11  ADD(S,E)->REG0        ADD(REG0,WEST)->REG1     ADD(REG1,NORTH)->EAST
//
// Nivel 1 (slot0, 1 ciclo): las 4 celdas suman su propio par de entradas
// EN PARALELO -- p0=v0+v1 (P00), p1=v2+v3 (P01), p2=v4+v5 (P10), p3=v6+v7
// (P11). P00/P10 escriben su resultado directo a EAST (visible para su
// vecino P01/P11 el ciclo siguiente); P01/P11 lo guardan en REG0 porque
// necesitan combinarlo con lo que llegue por WEST en el siguiente slot.
//
// Nivel 2 (slot1, 1 ciclo): P01 = reg0(p1) + WEST(p0, relevado por P00 en
// slot0) = fila_sup = p0+p1; escrito directo a SOUTH (visible para P11 el
// ciclo siguiente). P11 = reg0(p3) + WEST(p2, relevado por P10 en slot0) =
// fila_inf = p2+p3; se guarda en REG1 (necesita combinarse con NORTH en el
// siguiente slot).
//
// Nivel 3 (slot2, 1 ciclo): P11 = reg1(fila_inf) + NORTH(fila_sup, relevado
// por P01 en slot1) = total. Sale por el borde este externo de P11
// (out_E[fila 1]).
//
// 3 ciclos de computo (vs. 8 en el mapeo temporal) al costo de 4 celdas en
// vez de 1 -- exactamente el tradeoff espacio-por-tiempo que se quiere medir.

#ifndef SUM_REDUCTION_SPATIAL_MESH_C_H
#define SUM_REDUCTION_SPATIAL_MESH_C_H

#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int SUMRED_S_ROWS = 2;
static const int SUMRED_S_COLS = 2;
static const int SUMRED_S_DATA_W = 32;
static const int SUMRED_S_VLEN = 1;
static const int SUMRED_S_NUM_REGS = 8;
static const int SUMRED_S_INSTR_MEM_SIZE = 3;   // 3 niveles de arbol
static const int SUMRED_S_NUM_PHASES = 1;        // los 8 elementos entran de una

typedef PE_Scalar_State<SUMRED_S_DATA_W, SUMRED_S_VLEN, SUMRED_S_NUM_REGS, SUMRED_S_INSTR_MEM_SIZE>
    SumRedSpatialCell_C;

typedef CGRA_Mesh_Static_C<SUMRED_S_ROWS, SUMRED_S_COLS, SUMRED_S_DATA_W, SUMRED_S_VLEN,
                            SumRedSpatialCell_C, SumRedSpatialCell_C,
                            SumRedSpatialCell_C, SumRedSpatialCell_C>
    SumRedSpatialMesh_C;
typedef SumRedSpatialMesh_C::Link  SumRedSpatialLink_C;
typedef SumRedSpatialMesh_C::Instr SumRedSpatialInstr_C;

inline SumRedSpatialInstr_C sumred_add_instr_c(ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<3> dst) {
    SumRedSpatialInstr_C i;
    i.opcode = OP_ADD;
    i.src_a = src_a;
    i.src_b = src_b;
    i.dst = dst;
    return i;
}

inline SumRedSpatialInstr_C sumred_add_to_reg_instr_c(ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    SumRedSpatialInstr_C i;
    i.opcode = OP_ADD;
    i.src_a = src_a;
    i.src_b = src_b;
    i.dst = DST_REG;
    i.reg_dst = reg_dst;
    return i;
}

inline SumRedSpatialInstr_C sumred_add_reg_to_dst_instr_c(ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<3> dst) {
    SumRedSpatialInstr_C i;
    i.opcode = OP_ADD;
    i.src_a = SRC_REG;
    i.reg_a = reg_a;
    i.src_b = src_b;
    i.dst = dst;
    return i;
}

// Variante reg->reg: src_a=reg reg_a, dst=DST_REG reg_dst (el helper de
// arriba no sirve aca porque su `dst` es un PE_Dst generico sin reg_dst
// propio -- necesario para el nivel 2 de P11, que debe guardar en reg1 sin
// pisar reg0).
inline SumRedSpatialInstr_C sumred_add_reg_to_reg_instr_c(ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    SumRedSpatialInstr_C i;
    i.opcode = OP_ADD;
    i.src_a = SRC_REG;
    i.reg_a = reg_a;
    i.src_b = src_b;
    i.dst = DST_REG;
    i.reg_dst = reg_dst;
    return i;
}

// El programa espacial del arbol de sumas descrito en el encabezado.
inline void sumred_spatial_program_c(
    SumRedSpatialInstr_C prog[SUMRED_S_ROWS][SUMRED_S_COLS][SUMRED_S_INSTR_MEM_SIZE])
{
    // P00 (0,0): nivel1 = N+W, directo a EAST (hacia P01).
    prog[0][0][0] = sumred_add_instr_c(SRC_NORTH, SRC_WEST, DST_EAST);
    prog[0][0][1] = SumRedSpatialInstr_C();
    prog[0][0][2] = SumRedSpatialInstr_C();

    // P01 (0,1): nivel1 = N+E -> reg0; nivel2 = reg0+WEST(P00) -> SOUTH (hacia P11).
    prog[0][1][0] = sumred_add_to_reg_instr_c(SRC_NORTH, SRC_EAST, /*reg_dst=*/0);
    prog[0][1][1] = sumred_add_reg_to_dst_instr_c(/*reg_a=*/0, SRC_WEST, DST_SOUTH);
    prog[0][1][2] = SumRedSpatialInstr_C();

    // P10 (1,0): nivel1 = S+W, directo a EAST (hacia P11).
    prog[1][0][0] = sumred_add_instr_c(SRC_SOUTH, SRC_WEST, DST_EAST);
    prog[1][0][1] = SumRedSpatialInstr_C();
    prog[1][0][2] = SumRedSpatialInstr_C();

    // P11 (1,1): nivel1 = S+E -> reg0; nivel2 = reg0+WEST(P10) -> reg1;
    // nivel3 = reg1+NORTH(P01) -> EAST (total, sale por el borde externo).
    prog[1][1][0] = sumred_add_to_reg_instr_c(SRC_SOUTH, SRC_EAST, /*reg_dst=*/0);
    prog[1][1][1] = sumred_add_reg_to_reg_instr_c(/*reg_a=*/0, SRC_WEST, /*reg_dst=*/1);
    prog[1][1][2] = sumred_add_reg_to_dst_instr_c(/*reg_a=*/1, SRC_NORTH, DST_EAST);
}

#endif // SUM_REDUCTION_SPATIAL_MESH_C_H
