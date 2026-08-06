// matmul2x2_gem5_workload.c
// Baseline de CPU para multiplicacion de matrices 2x2 -- MISMOS casos de
// prueba que Proyecto_C/gemm_hls_c y gemm_temporal_hls_c, para comparar
// contra comparison_matrix_multiplication.md. Ver
// sum_reduction8_gem5_workload.c para el razonamiento (-O0, volatile).
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static -o matmul2x2_gem5_workload_arm \
//               matmul2x2_gem5_workload.c

#include <stdio.h>
#include <stdint.h>

static void matmul2x2_kernel(volatile int32_t A[2][2], volatile int32_t B[2][2],
                              int32_t C[2][2]) {
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += A[i][k] * B[k][j];
            C[i][j] = sum;
        }
    }
}

typedef struct {
    const char *label;
    int32_t A[2][2];
    int32_t B[2][2];
    int32_t expected_C[2][2];
} Round;

static const Round kRounds[] = {
    {"enteros positivos", {{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}, {{19, 22}, {43, 50}}},
    {"con valores negativos", {{-3, 5}, {2, -4}}, {{6, -1}, {-2, 3}}, {{-28, 18}, {20, -14}}},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        volatile int32_t A[2][2], B[2][2];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                A[i][j] = kRounds[r].A[i][j];
                B[i][j] = kRounds[r].B[i][j];
            }

        int32_t C[2][2];
        matmul2x2_kernel(A, B, C);

        int ok = 1;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                if (C[i][j] != kRounds[r].expected_C[i][j]) ok = 0;
        all_ok = all_ok && ok;

        printf("%s: C=[[%d,%d],[%d,%d]] expected=[[%d,%d],[%d,%d]] -> %s\n",
               kRounds[r].label, C[0][0], C[0][1], C[1][0], C[1][1],
               kRounds[r].expected_C[0][0], kRounds[r].expected_C[0][1],
               kRounds[r].expected_C[1][0], kRounds[r].expected_C[1][1],
               ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
