// SumReduction16_Spatial_Top_C.cpp -- definicion real (no inline), ver .h
#include "SumReduction16_Spatial_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void SumReduction16_Spatial_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    SumRed16SpatialInstr_C prog_instr,
    bool start, ap_uint<2> stage, bool& done,
    SumRed16SpatialLink_C in_N[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS],
    SumRed16SpatialLink_C in_S[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS],
    SumRed16SpatialLink_C in_W[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS],
    SumRed16SpatialLink_C in_E[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS],
    SumRed16SpatialLink_C out_N[SUMRED16_S_COLS], SumRed16SpatialLink_C out_S[SUMRED16_S_COLS],
    SumRed16SpatialLink_C out_W[SUMRED16_S_ROWS], SumRed16SpatialLink_C out_E[SUMRED16_S_ROWS])
{
    static SumRed16SpatialMesh_C mesh; // unico estado con memoria -- persiste entre etapas

    if (prog_valid) {
        // La rama de programar no depende de NUM_PHASES -- se usa la
        // instanciacion de etapa 1 (cualquiera sirve, ese branch de
        // cgra_run retorna antes de tocar fases).
        cgra_run<SUMRED16_S_ROWS, SUMRED16_S_COLS, SUMRED16_S_DATA_W, SUMRED16_S_VLEN,
                 SUMRED16_S_INSTR_MEM_SIZE, SUMRED16_S_STAGE1_NUM_PHASES>(
            mesh, true, prog_row, prog_col, prog_slot, prog_instr, false, done,
            in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
        return;
    }
    if (!start) return;

    if (stage == 1) {
        cgra_run<SUMRED16_S_ROWS, SUMRED16_S_COLS, SUMRED16_S_DATA_W, SUMRED16_S_VLEN,
                 SUMRED16_S_INSTR_MEM_SIZE, SUMRED16_S_STAGE1_NUM_PHASES>(
            mesh, false, 0, 0, 0, SumRed16SpatialInstr_C(), true, done,
            in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
        return;
    }

    // stage==0 (reset reg0) o stage==2 (combinar arbol): ambas solo
    // necesitan 1 fase -- usan el primer indice de los mismos arreglos
    // (dimensionados para la etapa 1, mas grande).
    SumRed16SpatialLink_C in_N1[1][SUMRED16_S_COLS], in_S1[1][SUMRED16_S_COLS];
    SumRed16SpatialLink_C in_W1[1][SUMRED16_S_ROWS], in_E1[1][SUMRED16_S_ROWS];
    for (int c = 0; c < SUMRED16_S_COLS; c++) {
        in_N1[0][c] = in_N[0][c];
        in_S1[0][c] = in_S[0][c];
    }
    for (int r = 0; r < SUMRED16_S_ROWS; r++) {
        in_W1[0][r] = in_W[0][r];
        in_E1[0][r] = in_E[0][r];
    }
    cgra_run<SUMRED16_S_ROWS, SUMRED16_S_COLS, SUMRED16_S_DATA_W, SUMRED16_S_VLEN,
             SUMRED16_S_INSTR_MEM_SIZE, SUMRED16_S_STAGE2_NUM_PHASES>(
        mesh, false, 0, 0, 0, SumRed16SpatialInstr_C(), true, done,
        in_N1, in_S1, in_W1, in_E1, out_N, out_S, out_W, out_E);
}
