// GEMM_2x2_Temporal_Top_C.h
// Wrapper delgado sobre cgra_run<...> para el mapeo TEMPORAL de GEMM 2x2:
// 1 celda (PE_MAC), 2 fases (k=0,1) por invocacion -- el host llama a este
// top 4 veces (una por C[i][j]) para completar la matriz. Mismo patron que
// GEMM_2x2_HLS_Top_C.h / SumReduction_Temporal_Top_C.h.

#ifndef GEMM_2X2_TEMPORAL_TOP_C_H
#define GEMM_2X2_TEMPORAL_TOP_C_H

#include "GEMM_2x2_Temporal_Mesh_C.h"

void GEMM_2x2_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    GemmTemporalInstr_C prog_instr,
    bool start, bool& done,
    GemmTemporalLink_C in_N[GEMM_T_NUM_PHASES][GEMM_T_COLS],
    GemmTemporalLink_C in_S[GEMM_T_NUM_PHASES][GEMM_T_COLS],
    GemmTemporalLink_C in_W[GEMM_T_NUM_PHASES][GEMM_T_ROWS],
    GemmTemporalLink_C in_E[GEMM_T_NUM_PHASES][GEMM_T_ROWS],
    GemmTemporalLink_C out_N[GEMM_T_COLS], GemmTemporalLink_C out_S[GEMM_T_COLS],
    GemmTemporalLink_C out_W[GEMM_T_ROWS], GemmTemporalLink_C out_E[GEMM_T_ROWS]);

#endif // GEMM_2X2_TEMPORAL_TOP_C_H
