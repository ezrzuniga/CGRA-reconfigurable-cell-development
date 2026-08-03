// CGRA_Hetero_2x2_Demo_Top_C.cpp
// Definicion de CGRA_Hetero_2x2_Demo_Top_C (ver el .h). Separada del header
// por el mismo motivo que GEMM_2x2_HLS_Top_C.cpp: Vitis HLS no reconoce un
// top marcado `inline`.

#include "CGRA_Hetero_2x2_Demo_Top_C.h"

void CGRA_Hetero_2x2_Demo_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot, HeteroInstr_C prog_instr,
    bool rst, bool enable,
    HeteroLink_C in_N[HETERO_COLS], HeteroLink_C in_S[HETERO_COLS],
    HeteroLink_C in_W[HETERO_ROWS], HeteroLink_C in_E[HETERO_ROWS],
    HeteroLink_C out_N[HETERO_COLS], HeteroLink_C out_S[HETERO_COLS],
    HeteroLink_C out_W[HETERO_ROWS], HeteroLink_C out_E[HETERO_ROWS])
{
    static HeteroMesh_C mesh;  // unico estado con memoria del diseno -- persiste entre llamadas

    if (prog_valid) {
        mesh_program(mesh, prog_row, prog_col, prog_slot, prog_instr);
        return;
    }

    mesh_step(mesh, rst, enable, in_N, in_S, in_W, in_E);

    HeteroLink_C all_out_N[HETERO_ROWS][HETERO_COLS], all_out_S[HETERO_ROWS][HETERO_COLS];
    HeteroLink_C all_out_E[HETERO_ROWS][HETERO_COLS], all_out_W[HETERO_ROWS][HETERO_COLS];
    mesh_read_outputs(mesh, all_out_N, all_out_S, all_out_E, all_out_W);

    for (int c = 0; c < HETERO_COLS; c++) {
#pragma HLS UNROLL
        out_N[c] = all_out_N[0][c];
        out_S[c] = all_out_S[HETERO_ROWS - 1][c];
    }
    for (int r = 0; r < HETERO_ROWS; r++) {
#pragma HLS UNROLL
        out_W[r] = all_out_W[r][0];
        out_E[r] = all_out_E[r][HETERO_COLS - 1];
    }
}
