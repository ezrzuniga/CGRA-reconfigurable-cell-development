// GEMM_2x2_Temporal_Top_C.cpp
// Definicion real (no inline) de GEMM_2x2_Temporal_Top_C -- ver
// GEMM_2x2_Temporal_Top_C.h.

#include "GEMM_2x2_Temporal_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void GEMM_2x2_Temporal_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    GemmTemporalInstr_C prog_instr,
    bool start, bool& done,
    GemmTemporalLink_C in_N[GEMM_T_NUM_PHASES][GEMM_T_COLS],
    GemmTemporalLink_C in_S[GEMM_T_NUM_PHASES][GEMM_T_COLS],
    GemmTemporalLink_C in_W[GEMM_T_NUM_PHASES][GEMM_T_ROWS],
    GemmTemporalLink_C in_E[GEMM_T_NUM_PHASES][GEMM_T_ROWS],
    GemmTemporalLink_C out_N[GEMM_T_COLS], GemmTemporalLink_C out_S[GEMM_T_COLS],
    GemmTemporalLink_C out_W[GEMM_T_ROWS], GemmTemporalLink_C out_E[GEMM_T_ROWS])
{
    static GemmTemporalMesh_C mesh; // unico estado con memoria del diseno

    cgra_run<GEMM_T_ROWS, GEMM_T_COLS, GEMM_T_DATA_W, GEMM_T_VLEN,
             GEMM_T_INSTR_MEM_SIZE, GEMM_T_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
