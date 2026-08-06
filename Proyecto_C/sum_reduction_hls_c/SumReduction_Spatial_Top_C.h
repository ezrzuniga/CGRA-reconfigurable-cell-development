// SumReduction_Spatial_Top_C.h
// Wrapper delgado sobre cgra_run<...> para el mapeo ESPACIAL de la reduccion
// por suma: 4 celdas (PE_Scalar en 2x2), 1 fase, arbol de sumas de 3 niveles.
// Mismo patron que GEMM_2x2_HLS_Top_C.h / SumReduction_Temporal_Top_C.h.

#ifndef SUM_REDUCTION_SPATIAL_TOP_C_H
#define SUM_REDUCTION_SPATIAL_TOP_C_H

#include "SumReduction_Spatial_Mesh_C.h"

void SumReduction_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    SumRedSpatialInstr_C prog_instr,
    bool start, bool& done,
    SumRedSpatialLink_C in_N[SUMRED_S_NUM_PHASES][SUMRED_S_COLS],
    SumRedSpatialLink_C in_S[SUMRED_S_NUM_PHASES][SUMRED_S_COLS],
    SumRedSpatialLink_C in_W[SUMRED_S_NUM_PHASES][SUMRED_S_ROWS],
    SumRedSpatialLink_C in_E[SUMRED_S_NUM_PHASES][SUMRED_S_ROWS],
    SumRedSpatialLink_C out_N[SUMRED_S_COLS], SumRedSpatialLink_C out_S[SUMRED_S_COLS],
    SumRedSpatialLink_C out_W[SUMRED_S_ROWS], SumRedSpatialLink_C out_E[SUMRED_S_ROWS]);

#endif // SUM_REDUCTION_SPATIAL_TOP_C_H
