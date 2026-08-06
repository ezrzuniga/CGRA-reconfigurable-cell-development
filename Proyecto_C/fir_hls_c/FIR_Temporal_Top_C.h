// FIR_Temporal_Top_C.h -- thin cgra_run<...> wrapper, same pattern as
// gemm_temporal_hls_c/GEMM_2x2_Temporal_Top_C.h.
#ifndef FIR_TEMPORAL_TOP_C_H
#define FIR_TEMPORAL_TOP_C_H

#include "FIR_Temporal_Mesh_C.h"

void FIR_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    FirTemporalInstr_C prog_instr,
    bool start, bool& done,
    FirTemporalLink_C in_N[FIR_T_NUM_PHASES][FIR_T_COLS],
    FirTemporalLink_C in_S[FIR_T_NUM_PHASES][FIR_T_COLS],
    FirTemporalLink_C in_W[FIR_T_NUM_PHASES][FIR_T_ROWS],
    FirTemporalLink_C in_E[FIR_T_NUM_PHASES][FIR_T_ROWS],
    FirTemporalLink_C out_N[FIR_T_COLS], FirTemporalLink_C out_S[FIR_T_COLS],
    FirTemporalLink_C out_W[FIR_T_ROWS], FirTemporalLink_C out_E[FIR_T_ROWS]);

#endif // FIR_TEMPORAL_TOP_C_H
