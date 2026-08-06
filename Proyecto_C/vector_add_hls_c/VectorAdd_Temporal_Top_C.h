#ifndef VECTORADD_TEMPORAL_TOP_C_H
#define VECTORADD_TEMPORAL_TOP_C_H

#include "VectorAdd_Temporal_Mesh_C.h"

void VectorAdd_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    VectorAddTemporalInstr_C prog_instr,
    bool start, bool& done,
    VectorAddTemporalLink_C in_N[VADD_T_NUM_PHASES][VADD_T_COLS],
    VectorAddTemporalLink_C in_S[VADD_T_NUM_PHASES][VADD_T_COLS],
    VectorAddTemporalLink_C in_W[VADD_T_NUM_PHASES][VADD_T_ROWS],
    VectorAddTemporalLink_C in_E[VADD_T_NUM_PHASES][VADD_T_ROWS],
    VectorAddTemporalLink_C out_N[VADD_T_COLS], VectorAddTemporalLink_C out_S[VADD_T_COLS],
    VectorAddTemporalLink_C out_W[VADD_T_ROWS], VectorAddTemporalLink_C out_E[VADD_T_ROWS]);

#endif // VECTORADD_TEMPORAL_TOP_C_H
