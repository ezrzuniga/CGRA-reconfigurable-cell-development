#include "VectorAdd_Temporal_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void VectorAdd_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    VectorAddTemporalInstr_C prog_instr,
    bool start, bool& done,
    VectorAddTemporalLink_C in_N[VADD_T_NUM_PHASES][VADD_T_COLS],
    VectorAddTemporalLink_C in_S[VADD_T_NUM_PHASES][VADD_T_COLS],
    VectorAddTemporalLink_C in_W[VADD_T_NUM_PHASES][VADD_T_ROWS],
    VectorAddTemporalLink_C in_E[VADD_T_NUM_PHASES][VADD_T_ROWS],
    VectorAddTemporalLink_C out_N[VADD_T_COLS], VectorAddTemporalLink_C out_S[VADD_T_COLS],
    VectorAddTemporalLink_C out_W[VADD_T_ROWS], VectorAddTemporalLink_C out_E[VADD_T_ROWS])
{
    static VectorAddTemporalMesh_C mesh;
    cgra_run<VADD_T_ROWS, VADD_T_COLS, VADD_T_DATA_W, VADD_T_VLEN,
             VADD_T_INSTR_MEM_SIZE, VADD_T_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
