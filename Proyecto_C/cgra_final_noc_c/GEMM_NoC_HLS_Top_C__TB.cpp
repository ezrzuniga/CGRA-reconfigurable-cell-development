// GEMM_NoC_HLS_Top_C__TB.cpp
// Testbench plano (sin sc_main, sin conocer nada de la malla) para
// GEMM_NoC_HLS_Top_C: llama al top como caja negra con 3 pares de matrices
// 2x2 (misma semilla 20260810 que CGRA_Final_NoC_GEMM_C__TB.cpp) y compara
// contra C = A*B calculado en C++ puro. Sirve como csim de Vitis HLS y como
// validacion standalone con g++.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "GEMM_NoC_HLS_Top_C.h"

static void gen_random_matrix(int32_t M[2][2], int lo, int hi) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            M[i][j] = lo + std::rand() % (hi - lo + 1);
}

static bool run_case(int case_num, const int32_t A[2][2], const int32_t B[2][2]) {
    int32_t C_ref[2][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += A[i][k] * B[k][j];
            C_ref[i][j] = sum;
        }

    int32_t C_got[2][2];
    GEMM_NoC_HLS_Top_C(A, B, C_got);

    bool ok = true;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            if (C_got[i][j] != C_ref[i][j]) ok = false;

    printf("CASO %d: A=[[%d,%d],[%d,%d]] B=[[%d,%d],[%d,%d]]\n", case_num,
           A[0][0], A[0][1], A[1][0], A[1][1], B[0][0], B[0][1], B[1][0], B[1][1]);
    printf("  C esperado = [[%d,%d],[%d,%d]]\n", C_ref[0][0], C_ref[0][1], C_ref[1][0], C_ref[1][1]);
    printf("  C obtenido = [[%d,%d],[%d,%d]]\n", C_got[0][0], C_got[0][1], C_got[1][0], C_got[1][1]);
    printf("  Resultado: %s\n\n", ok ? "PASS" : "FAIL");
    return ok;
}

int main() {
    printf("############################################################\n"
           "#  GEMM 2x2 sobre GEMM_NoC_HLS_Top_C (malla NoC 3x3 real)   #\n"
           "############################################################\n\n");

    std::srand(20260810);
    int32_t A[3][2][2], B[3][2][2];
    for (int c = 0; c < 3; c++) {
        gen_random_matrix(A[c], -9, 9);
        gen_random_matrix(B[c], -9, 9);
    }

    bool all_ok = true;
    for (int c = 0; c < 3; c++)
        all_ok = run_case(c + 1, A[c], B[c]) && all_ok;

    printf("============================================================\n");
    printf(all_ok ? "RESULTADO: PASS (los 3 casos)\n" : "RESULTADO: FAIL\n");
    printf("============================================================\n");
    return all_ok ? 0 : 1;
}
