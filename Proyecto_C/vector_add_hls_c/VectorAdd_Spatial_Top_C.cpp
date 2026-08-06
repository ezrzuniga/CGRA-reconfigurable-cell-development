#include "VectorAdd_Spatial_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void VectorAdd_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    VectorAddSpatialInstr_C prog_instr,
    bool start, bool& done,
    VectorAddSpatialLink_C in_N[VADD_S_NUM_PHASES][VADD_S_COLS],
    VectorAddSpatialLink_C in_S[VADD_S_NUM_PHASES][VADD_S_COLS],
    VectorAddSpatialLink_C in_W[VADD_S_NUM_PHASES][VADD_S_ROWS],
    VectorAddSpatialLink_C in_E[VADD_S_NUM_PHASES][VADD_S_ROWS],
    VectorAddSpatialLink_C out_N[VADD_S_COLS], VectorAddSpatialLink_C out_S[VADD_S_COLS],
    VectorAddSpatialLink_C out_W[VADD_S_ROWS], VectorAddSpatialLink_C out_E[VADD_S_ROWS])
{
    static VectorAddSpatialMesh_C mesh;
    cgra_run<VADD_S_ROWS, VADD_S_COLS, VADD_S_DATA_W, VADD_S_VLEN,
             VADD_S_INSTR_MEM_SIZE, VADD_S_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
