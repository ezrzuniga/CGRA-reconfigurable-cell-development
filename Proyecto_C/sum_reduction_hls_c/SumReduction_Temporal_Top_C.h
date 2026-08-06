// SumReduction_Temporal_Top_C.h
// Wrapper delgado sobre cgra_run<...> (mismo patron que
// gemm_hls_c/GEMM_2x2_HLS_Top_C.h) para el mapeo TEMPORAL de la reduccion
// por suma: 1 celda (PE_MAC), 8 fases (una por elemento). Declaracion
// separada de la definicion (.cpp) por la misma razon que GEMM: Vitis HLS no
// encuentra un top `inline`.

#ifndef SUM_REDUCTION_TEMPORAL_TOP_C_H
#define SUM_REDUCTION_TEMPORAL_TOP_C_H

#include "SumReduction_Temporal_Mesh_C.h"

void SumReduction_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    SumRedTemporalInstr_C prog_instr,
    bool start, bool& done,
    SumRedTemporalLink_C in_N[SUMRED_T_NUM_PHASES][SUMRED_T_COLS],
    SumRedTemporalLink_C in_S[SUMRED_T_NUM_PHASES][SUMRED_T_COLS],
    SumRedTemporalLink_C in_W[SUMRED_T_NUM_PHASES][SUMRED_T_ROWS],
    SumRedTemporalLink_C in_E[SUMRED_T_NUM_PHASES][SUMRED_T_ROWS],
    SumRedTemporalLink_C out_N[SUMRED_T_COLS], SumRedTemporalLink_C out_S[SUMRED_T_COLS],
    SumRedTemporalLink_C out_W[SUMRED_T_ROWS], SumRedTemporalLink_C out_E[SUMRED_T_ROWS]);

#endif // SUM_REDUCTION_TEMPORAL_TOP_C_H
