// SumReduction_Temporal_Top_C.cpp
// Definicion real (no inline) de SumReduction_Temporal_Top_C -- ver
// SumReduction_Temporal_Top_C.h.

#include "SumReduction_Temporal_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void SumReduction_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    SumRedTemporalInstr_C prog_instr,
    bool start, bool& done,
    SumRedTemporalLink_C in_N[SUMRED_T_NUM_PHASES][SUMRED_T_COLS],
    SumRedTemporalLink_C in_S[SUMRED_T_NUM_PHASES][SUMRED_T_COLS],
    SumRedTemporalLink_C in_W[SUMRED_T_NUM_PHASES][SUMRED_T_ROWS],
    SumRedTemporalLink_C in_E[SUMRED_T_NUM_PHASES][SUMRED_T_ROWS],
    SumRedTemporalLink_C out_N[SUMRED_T_COLS], SumRedTemporalLink_C out_S[SUMRED_T_COLS],
    SumRedTemporalLink_C out_W[SUMRED_T_ROWS], SumRedTemporalLink_C out_E[SUMRED_T_ROWS])
{
    static SumRedTemporalMesh_C mesh; // unico estado con memoria del diseno

    cgra_run<SUMRED_T_ROWS, SUMRED_T_COLS, SUMRED_T_DATA_W, SUMRED_T_VLEN,
             SUMRED_T_INSTR_MEM_SIZE, SUMRED_T_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
