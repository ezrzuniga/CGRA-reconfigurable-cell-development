// SumReduction16_Temporal_Top_C.h -- thin cgra_run<...> wrapper.
#ifndef SUM_REDUCTION16_TEMPORAL_TOP_C_H
#define SUM_REDUCTION16_TEMPORAL_TOP_C_H

#include "SumReduction16_Temporal_Mesh_C.h"

void SumReduction16_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    SumRed16TemporalInstr_C prog_instr,
    bool start, bool& done,
    SumRed16TemporalLink_C in_N[SUMRED16_T_NUM_PHASES][SUMRED16_T_COLS],
    SumRed16TemporalLink_C in_S[SUMRED16_T_NUM_PHASES][SUMRED16_T_COLS],
    SumRed16TemporalLink_C in_W[SUMRED16_T_NUM_PHASES][SUMRED16_T_ROWS],
    SumRed16TemporalLink_C in_E[SUMRED16_T_NUM_PHASES][SUMRED16_T_ROWS],
    SumRed16TemporalLink_C out_N[SUMRED16_T_COLS], SumRed16TemporalLink_C out_S[SUMRED16_T_COLS],
    SumRed16TemporalLink_C out_W[SUMRED16_T_ROWS], SumRed16TemporalLink_C out_E[SUMRED16_T_ROWS]);

#endif // SUM_REDUCTION16_TEMPORAL_TOP_C_H
