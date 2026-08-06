// FIR_Spatial_Top_C.cpp -- definicion real (no inline), ver .h
#include "FIR_Spatial_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void FIR_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    FirSpatialInstr_C prog_instr,
    bool start, bool& done,
    FirSpatialLink_C in_N[FIR_S_NUM_PHASES][FIR_S_COLS],
    FirSpatialLink_C in_S[FIR_S_NUM_PHASES][FIR_S_COLS],
    FirSpatialLink_C in_W[FIR_S_NUM_PHASES][FIR_S_ROWS],
    FirSpatialLink_C in_E[FIR_S_NUM_PHASES][FIR_S_ROWS],
    FirSpatialLink_C out_N[FIR_S_COLS], FirSpatialLink_C out_S[FIR_S_COLS],
    FirSpatialLink_C out_W[FIR_S_ROWS], FirSpatialLink_C out_E[FIR_S_ROWS])
{
    static FirSpatialMesh_C mesh;
    cgra_run<FIR_S_ROWS, FIR_S_COLS, FIR_S_DATA_W, FIR_S_VLEN,
             FIR_S_INSTR_MEM_SIZE, FIR_S_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
