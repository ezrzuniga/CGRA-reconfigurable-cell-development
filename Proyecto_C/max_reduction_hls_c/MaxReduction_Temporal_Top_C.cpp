// MaxReduction_Temporal_Top_C.cpp -- definicion real (no inline), ver .h
#include "MaxReduction_Temporal_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void MaxReduction_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    MaxRedTemporalInstr_C prog_instr,
    bool start, bool& done,
    MaxRedTemporalLink_C in_N[MAXRED_T_NUM_PHASES][MAXRED_T_COLS],
    MaxRedTemporalLink_C in_S[MAXRED_T_NUM_PHASES][MAXRED_T_COLS],
    MaxRedTemporalLink_C in_W[MAXRED_T_NUM_PHASES][MAXRED_T_ROWS],
    MaxRedTemporalLink_C in_E[MAXRED_T_NUM_PHASES][MAXRED_T_ROWS],
    MaxRedTemporalLink_C out_N[MAXRED_T_COLS], MaxRedTemporalLink_C out_S[MAXRED_T_COLS],
    MaxRedTemporalLink_C out_W[MAXRED_T_ROWS], MaxRedTemporalLink_C out_E[MAXRED_T_ROWS])
{
    static MaxRedTemporalMesh_C mesh;
    cgra_run<MAXRED_T_ROWS, MAXRED_T_COLS, MAXRED_T_DATA_W, MAXRED_T_VLEN,
             MAXRED_T_INSTR_MEM_SIZE, MAXRED_T_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
