// SumReduction16_Temporal_Top_C.cpp -- definicion real (no inline), ver .h
#include "SumReduction16_Temporal_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void SumReduction16_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    SumRed16TemporalInstr_C prog_instr,
    bool start, bool& done,
    SumRed16TemporalLink_C in_N[SUMRED16_T_NUM_PHASES][SUMRED16_T_COLS],
    SumRed16TemporalLink_C in_S[SUMRED16_T_NUM_PHASES][SUMRED16_T_COLS],
    SumRed16TemporalLink_C in_W[SUMRED16_T_NUM_PHASES][SUMRED16_T_ROWS],
    SumRed16TemporalLink_C in_E[SUMRED16_T_NUM_PHASES][SUMRED16_T_ROWS],
    SumRed16TemporalLink_C out_N[SUMRED16_T_COLS], SumRed16TemporalLink_C out_S[SUMRED16_T_COLS],
    SumRed16TemporalLink_C out_W[SUMRED16_T_ROWS], SumRed16TemporalLink_C out_E[SUMRED16_T_ROWS])
{
    static SumRed16TemporalMesh_C mesh;
    cgra_run<SUMRED16_T_ROWS, SUMRED16_T_COLS, SUMRED16_T_DATA_W, SUMRED16_T_VLEN,
             SUMRED16_T_INSTR_MEM_SIZE, SUMRED16_T_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
