// FIR_Spatial_Top_C.h -- thin cgra_run<...> wrapper.
#ifndef FIR_SPATIAL_TOP_C_H
#define FIR_SPATIAL_TOP_C_H

#include "FIR_Spatial_Mesh_C.h"

void FIR_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    FirSpatialInstr_C prog_instr,
    bool start, bool& done,
    FirSpatialLink_C in_N[FIR_S_NUM_PHASES][FIR_S_COLS],
    FirSpatialLink_C in_S[FIR_S_NUM_PHASES][FIR_S_COLS],
    FirSpatialLink_C in_W[FIR_S_NUM_PHASES][FIR_S_ROWS],
    FirSpatialLink_C in_E[FIR_S_NUM_PHASES][FIR_S_ROWS],
    FirSpatialLink_C out_N[FIR_S_COLS], FirSpatialLink_C out_S[FIR_S_COLS],
    FirSpatialLink_C out_W[FIR_S_ROWS], FirSpatialLink_C out_E[FIR_S_ROWS]);

#endif // FIR_SPATIAL_TOP_C_H
