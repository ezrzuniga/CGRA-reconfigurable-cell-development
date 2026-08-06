#ifndef VECTORADD_SPATIAL_TOP_C_H
#define VECTORADD_SPATIAL_TOP_C_H

#include "VectorAdd_Spatial_Mesh_C.h"

void VectorAdd_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    VectorAddSpatialInstr_C prog_instr,
    bool start, bool& done,
    VectorAddSpatialLink_C in_N[VADD_S_NUM_PHASES][VADD_S_COLS],
    VectorAddSpatialLink_C in_S[VADD_S_NUM_PHASES][VADD_S_COLS],
    VectorAddSpatialLink_C in_W[VADD_S_NUM_PHASES][VADD_S_ROWS],
    VectorAddSpatialLink_C in_E[VADD_S_NUM_PHASES][VADD_S_ROWS],
    VectorAddSpatialLink_C out_N[VADD_S_COLS], VectorAddSpatialLink_C out_S[VADD_S_COLS],
    VectorAddSpatialLink_C out_W[VADD_S_ROWS], VectorAddSpatialLink_C out_E[VADD_S_ROWS]);

#endif // VECTORADD_SPATIAL_TOP_C_H
