// matmul2x2_cgra_gem5_workload.c
// CGRA-offload counterpart to matmul2x2_gem5_workload.c: same 2 test
// cases, but C is computed by the real CGRA temporal AND spatial designs
// via m5_cgra_run -- see gem5/src/cgra/cgra_kernels.{hh,cc} for the bridge
// (drives the actual Proyecto_C/gemm_hls_c/ and
// Proyecto_C/gemm_temporal_hls_c/ models synthesized/cosim'd for
// comparison_matrix_multiplication.md).
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static \
//               -o matmul2x2_cgra_gem5_workload_arm \
//               matmul2x2_cgra_gem5_workload.c m5ops_arm64/m5op.o

#include <stdio.h>
#include <stdint.h>

// Must match gem5/src/cgra/cgra_kernels.hh.
#define KERNEL_MATMUL2X2_TEMPORAL 10
#define KERNEL_MATMUL2X2_SPATIAL 11

typedef struct {
    int32_t A[2][2];
    int32_t B[2][2];
    int32_t C[2][2];
} CgraMatmul2x2Args;

extern uint64_t m5_cgra_run(uint64_t kernel_id, uint64_t args_ptr);

static void matmul2x2_cgra(uint64_t kernel_id, int32_t A[2][2], int32_t B[2][2],
                            int32_t C[2][2], uint64_t *cycles_out) {
    CgraMatmul2x2Args args;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) { args.A[i][j] = A[i][j]; args.B[i][j] = B[i][j]; }
    *cycles_out = m5_cgra_run(kernel_id, (uint64_t)(uintptr_t)&args);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) C[i][j] = args.C[i][j];
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
        uint64_t cyc_t, cyc_s;
        int32_t A[2][2], B[2][2], C_t[2][2], C_s[2][2];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) { A[i][j] = kRounds[r].A[i][j]; B[i][j] = kRounds[r].B[i][j]; }

        matmul2x2_cgra(KERNEL_MATMUL2X2_TEMPORAL, A, B, C_t, &cyc_t);
        matmul2x2_cgra(KERNEL_MATMUL2X2_SPATIAL, A, B, C_s, &cyc_s);

        int ok_t = 1, ok_s = 1;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                if (C_t[i][j] != kRounds[r].expected_C[i][j]) ok_t = 0;
                if (C_s[i][j] != kRounds[r].expected_C[i][j]) ok_s = 0;
            }
        }
        all_ok = all_ok && ok_t && ok_s;

        printf("%s:\n", kRounds[r].label);
        printf("  temporal: C=[[%d,%d],[%d,%d]] expected=[[%d,%d],[%d,%d]] cycles=%llu -> %s\n",
               C_t[0][0], C_t[0][1], C_t[1][0], C_t[1][1],
               kRounds[r].expected_C[0][0], kRounds[r].expected_C[0][1],
               kRounds[r].expected_C[1][0], kRounds[r].expected_C[1][1],
               (unsigned long long)cyc_t, ok_t ? "PASS" : "FAIL");
        printf("  spatial : C=[[%d,%d],[%d,%d]] expected=[[%d,%d],[%d,%d]] cycles=%llu -> %s\n",
               C_s[0][0], C_s[0][1], C_s[1][0], C_s[1][1],
               kRounds[r].expected_C[0][0], kRounds[r].expected_C[0][1],
               kRounds[r].expected_C[1][0], kRounds[r].expected_C[1][1],
               (unsigned long long)cyc_s, ok_s ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
