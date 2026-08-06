// SumReduction_Spatial_Top_C.cpp
// Definicion real (no inline) de SumReduction_Spatial_Top_C -- ver
// SumReduction_Spatial_Top_C.h.

#include "SumReduction_Spatial_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void SumReduction_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    SumRedSpatialInstr_C prog_instr,
    bool start, bool& done,
    SumRedSpatialLink_C in_N[SUMRED_S_NUM_PHASES][SUMRED_S_COLS],
    SumRedSpatialLink_C in_S[SUMRED_S_NUM_PHASES][SUMRED_S_COLS],
    SumRedSpatialLink_C in_W[SUMRED_S_NUM_PHASES][SUMRED_S_ROWS],
    SumRedSpatialLink_C in_E[SUMRED_S_NUM_PHASES][SUMRED_S_ROWS],
    SumRedSpatialLink_C out_N[SUMRED_S_COLS], SumRedSpatialLink_C out_S[SUMRED_S_COLS],
    SumRedSpatialLink_C out_W[SUMRED_S_ROWS], SumRedSpatialLink_C out_E[SUMRED_S_ROWS])
{
    static SumRedSpatialMesh_C mesh; // unico estado con memoria del diseno

    cgra_run<SUMRED_S_ROWS, SUMRED_S_COLS, SUMRED_S_DATA_W, SUMRED_S_VLEN,
             SUMRED_S_INSTR_MEM_SIZE, SUMRED_S_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
