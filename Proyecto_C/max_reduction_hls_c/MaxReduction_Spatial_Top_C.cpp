// MaxReduction_Spatial_Top_C.cpp -- definicion real (no inline), ver .h
#include "MaxReduction_Spatial_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void MaxReduction_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    MaxRedSpatialInstr_C prog_instr,
    bool start, bool& done,
    MaxRedSpatialLink_C in_N[MAXRED_S_NUM_PHASES][MAXRED_S_COLS],
    MaxRedSpatialLink_C in_S[MAXRED_S_NUM_PHASES][MAXRED_S_COLS],
    MaxRedSpatialLink_C in_W[MAXRED_S_NUM_PHASES][MAXRED_S_ROWS],
    MaxRedSpatialLink_C in_E[MAXRED_S_NUM_PHASES][MAXRED_S_ROWS],
    MaxRedSpatialLink_C out_N[MAXRED_S_COLS], MaxRedSpatialLink_C out_S[MAXRED_S_COLS],
    MaxRedSpatialLink_C out_W[MAXRED_S_ROWS], MaxRedSpatialLink_C out_E[MAXRED_S_ROWS])
{
    static MaxRedSpatialMesh_C mesh;
    cgra_run<MAXRED_S_ROWS, MAXRED_S_COLS, MAXRED_S_DATA_W, MAXRED_S_VLEN,
             MAXRED_S_INSTR_MEM_SIZE, MAXRED_S_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
