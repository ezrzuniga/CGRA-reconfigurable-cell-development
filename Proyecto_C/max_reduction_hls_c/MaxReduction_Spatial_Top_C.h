// MaxReduction_Spatial_Top_C.h -- thin cgra_run<...> wrapper.
#ifndef MAX_REDUCTION_SPATIAL_TOP_C_H
#define MAX_REDUCTION_SPATIAL_TOP_C_H

#include "MaxReduction_Spatial_Mesh_C.h"

void MaxReduction_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    MaxRedSpatialInstr_C prog_instr,
    bool start, bool& done,
    MaxRedSpatialLink_C in_N[MAXRED_S_NUM_PHASES][MAXRED_S_COLS],
    MaxRedSpatialLink_C in_S[MAXRED_S_NUM_PHASES][MAXRED_S_COLS],
    MaxRedSpatialLink_C in_W[MAXRED_S_NUM_PHASES][MAXRED_S_ROWS],
    MaxRedSpatialLink_C in_E[MAXRED_S_NUM_PHASES][MAXRED_S_ROWS],
    MaxRedSpatialLink_C out_N[MAXRED_S_COLS], MaxRedSpatialLink_C out_S[MAXRED_S_COLS],
    MaxRedSpatialLink_C out_W[MAXRED_S_ROWS], MaxRedSpatialLink_C out_E[MAXRED_S_ROWS]);

#endif // MAX_REDUCTION_SPATIAL_TOP_C_H
