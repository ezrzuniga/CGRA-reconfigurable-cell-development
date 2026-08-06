// FIR_Temporal_Top_C.cpp -- definicion real (no inline), ver .h
#include "FIR_Temporal_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void FIR_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    FirTemporalInstr_C prog_instr,
    bool start, bool& done,
    FirTemporalLink_C in_N[FIR_T_NUM_PHASES][FIR_T_COLS],
    FirTemporalLink_C in_S[FIR_T_NUM_PHASES][FIR_T_COLS],
    FirTemporalLink_C in_W[FIR_T_NUM_PHASES][FIR_T_ROWS],
    FirTemporalLink_C in_E[FIR_T_NUM_PHASES][FIR_T_ROWS],
    FirTemporalLink_C out_N[FIR_T_COLS], FirTemporalLink_C out_S[FIR_T_COLS],
    FirTemporalLink_C out_W[FIR_T_ROWS], FirTemporalLink_C out_E[FIR_T_ROWS])
{
    static FirTemporalMesh_C mesh;
    cgra_run<FIR_T_ROWS, FIR_T_COLS, FIR_T_DATA_W, FIR_T_VLEN,
             FIR_T_INSTR_MEM_SIZE, FIR_T_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
