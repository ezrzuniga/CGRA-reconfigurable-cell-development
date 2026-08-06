// MaxReduction_Temporal_Top_C.h -- thin cgra_run<...> wrapper.
#ifndef MAX_REDUCTION_TEMPORAL_TOP_C_H
#define MAX_REDUCTION_TEMPORAL_TOP_C_H

#include "MaxReduction_Temporal_Mesh_C.h"

void MaxReduction_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    MaxRedTemporalInstr_C prog_instr,
    bool start, bool& done,
    MaxRedTemporalLink_C in_N[MAXRED_T_NUM_PHASES][MAXRED_T_COLS],
    MaxRedTemporalLink_C in_S[MAXRED_T_NUM_PHASES][MAXRED_T_COLS],
    MaxRedTemporalLink_C in_W[MAXRED_T_NUM_PHASES][MAXRED_T_ROWS],
    MaxRedTemporalLink_C in_E[MAXRED_T_NUM_PHASES][MAXRED_T_ROWS],
    MaxRedTemporalLink_C out_N[MAXRED_T_COLS], MaxRedTemporalLink_C out_S[MAXRED_T_COLS],
    MaxRedTemporalLink_C out_W[MAXRED_T_ROWS], MaxRedTemporalLink_C out_E[MAXRED_T_ROWS]);

#endif // MAX_REDUCTION_TEMPORAL_TOP_C_H
