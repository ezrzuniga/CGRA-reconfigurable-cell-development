// sum_reduction16_cgra_gem5_workload.c
// CGRA-offload counterpart to sum_reduction16_gem5_workload.c: same 2 test
// vectors, but total is computed by the real CGRA temporal AND spatial
// (3-stage) designs via m5_cgra_run -- see
// gem5/src/cgra/cgra_kernels.{hh,cc} for the bridge (drives the actual
// Proyecto_C/sum_reduction16_hls_c/ models synthesized/cosim'd for
// comparison_sum_reduction_n16.md).
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static \
//               -o sum_reduction16_cgra_gem5_workload_arm \
//               sum_reduction16_cgra_gem5_workload.c m5ops_arm64/m5op.o

#include <stdio.h>
#include <stdint.h>

#define N 16

// Must match gem5/src/cgra/cgra_kernels.hh.
#define KERNEL_SUM_REDUCTION16_TEMPORAL 4
#define KERNEL_SUM_REDUCTION16_SPATIAL 5

typedef struct {
    int32_t v[16];
    int32_t total;
} CgraSumReduction16Args;

extern uint64_t m5_cgra_run(uint64_t kernel_id, uint64_t args_ptr);

static int32_t sum_reduction16_cgra(uint64_t kernel_id, const int32_t *v, uint64_t *cycles_out) {
    CgraSumReduction16Args args;
    for (int i = 0; i < N; i++) args.v[i] = v[i];
    *cycles_out = m5_cgra_run(kernel_id, (uint64_t)(uintptr_t)&args);
    return args.total;
}

typedef struct {
    const char *label;
    int32_t v[N];
    int32_t expected_total;
} Round;

static const Round kRounds[] = {
    {"v16a", {6, -2, 9, 4, 0, 7, -5, 3, 2, -8, 6, 1, -3, 5, 4, -1}, 28},
    {"v16b", {10, 20, -5, 3, 7, -2, 8, 1, 4, -6, 9, 0, 2, -1, 5, 3}, 58},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        uint64_t cyc_t, cyc_s;
        int32_t total_t = sum_reduction16_cgra(KERNEL_SUM_REDUCTION16_TEMPORAL, kRounds[r].v, &cyc_t);
        int32_t total_s = sum_reduction16_cgra(KERNEL_SUM_REDUCTION16_SPATIAL, kRounds[r].v, &cyc_s);

        int ok_t = (total_t == kRounds[r].expected_total);
        int ok_s = (total_s == kRounds[r].expected_total);
        all_ok = all_ok && ok_t && ok_s;

        printf("%s:\n", kRounds[r].label);
        printf("  temporal: total=%d expected=%d cycles=%llu -> %s\n",
               total_t, kRounds[r].expected_total, (unsigned long long)cyc_t, ok_t ? "PASS" : "FAIL");
        printf("  spatial : total=%d expected=%d cycles=%llu -> %s\n",
               total_s, kRounds[r].expected_total, (unsigned long long)cyc_s, ok_s ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
