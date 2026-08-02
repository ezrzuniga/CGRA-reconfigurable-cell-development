// GEMM_2x2_HLS_Top_C.cpp
// Definicion de GEMM_2x2_HLS_Top_C (ver GEMM_2x2_HLS_Top_C.h para el diseno
// completo). Separada del header para que Vitis HLS encuentre el top como
// una funcion real (no inline) al analizar la unidad de traduccion del
// diseno.

#include "GEMM_2x2_HLS_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void GEMM_2x2_HLS_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    GemmInstr_C prog_instr,
    bool start, bool& done,
    GemmLink_C in_N[GEMM_NUM_PHASES][GEMM_COLS], GemmLink_C in_S[GEMM_NUM_PHASES][GEMM_COLS],
    GemmLink_C in_W[GEMM_NUM_PHASES][GEMM_ROWS], GemmLink_C in_E[GEMM_NUM_PHASES][GEMM_ROWS],
    GemmLink_C out_N[GEMM_COLS], GemmLink_C out_S[GEMM_COLS],
    GemmLink_C out_W[GEMM_ROWS], GemmLink_C out_E[GEMM_ROWS])
{
    static GemmMesh_C mesh; // unico estado con memoria del diseno -- persiste entre llamadas

    cgra_run<GEMM_ROWS, GEMM_COLS, GEMM_DATA_W, GEMM_VLEN, GEMM_NUM_REGS,
             GEMM_INSTR_MEM_SIZE, GEMM_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
