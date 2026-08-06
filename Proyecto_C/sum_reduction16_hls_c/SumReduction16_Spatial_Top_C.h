// SumReduction16_Spatial_Top_C.h
// A diferencia de los demas tops de este repo, este necesita un parametro
// extra `stage` (0=reset reg0, 1=etapa1 acumular hojas, 2=etapa2 combinar
// arbol): el diseno de 3 pasadas (ver SumReduction16_Spatial_Mesh_C.h) llama
// a cgra_run<...> con NUM_PHASES=1/2/1 respectivamente -- tres
// instanciaciones DISTINTAS de la misma plantilla sobre el MISMO
// `static mesh` persistente (NUM_PHASES es un parametro de plantilla de
// cgra_run, no del tipo de la malla, asi que esto es valido; ver el .cpp).
// Los puertos de borde quedan dimensionados para la etapa 1 (la mas grande,
// 2 fases); las etapas 0 y 2 solo usan el primer indice de fase de esos
// mismos arreglos.
//
// Secuencia de uso completa (ver el testbench para el detalle):
//   0) prog_valid x28 con el programa de reset (sumred16_stage0_reset_program_c)
//   1) start=true, stage=0 -- limpia reg0 en las 4 celdas (necesario porque
//      el mesh es `static` y reg_file no tiene equivalente de
//      mesh_clear_acc(); sin este paso una segunda corrida sobre el mismo
//      mesh acumularia sobre el reg0 que dejo la corrida anterior)
//   2) prog_valid x28 con el programa de etapa 1 (sumred16_stage1_program_all_c)
//   3) start=true, stage=1 -- corre la etapa 1 (acumula hojas, 2 fases)
//   4) prog_valid x28 con el programa de etapa 2 (sumred16_stage2_program_c)
//      -- reprogramar NO toca reg_file, los reg0 acumulados sobreviven
//   5) start=true, stage=2 -- corre la etapa 2 (combina arbol), out_E[1]
//      tiene el resultado final

#ifndef SUM_REDUCTION16_SPATIAL_TOP_C_H
#define SUM_REDUCTION16_SPATIAL_TOP_C_H

#include "SumReduction16_Spatial_Mesh_C.h"

void SumReduction16_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    SumRed16SpatialInstr_C prog_instr,
    bool start, ap_uint<2> stage, bool& done,
    SumRed16SpatialLink_C in_N[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS],
    SumRed16SpatialLink_C in_S[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS],
    SumRed16SpatialLink_C in_W[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS],
    SumRed16SpatialLink_C in_E[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS],
    SumRed16SpatialLink_C out_N[SUMRED16_S_COLS], SumRed16SpatialLink_C out_S[SUMRED16_S_COLS],
    SumRed16SpatialLink_C out_W[SUMRED16_S_ROWS], SumRed16SpatialLink_C out_E[SUMRED16_S_ROWS]);

#endif // SUM_REDUCTION16_SPATIAL_TOP_C_H
