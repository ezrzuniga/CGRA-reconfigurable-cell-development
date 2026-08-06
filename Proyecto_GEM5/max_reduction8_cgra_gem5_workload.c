// max_reduction8_cgra_gem5_workload.c
// CGRA-offload counterpart to max_reduction8_gem5_workload.c: same 2 test
// vectors, but total is computed by the real CGRA temporal AND spatial
// designs via m5_cgra_run -- see gem5/src/cgra/cgra_kernels.{hh,cc} for the
// bridge (drives the actual Proyecto_C/max_reduction_hls_c/ models
// synthesized/cosim'd for comparison_max_reduction.md).
//
// IMPORTANT: the temporal design's reg0 is self-referential (no native MAX
// opcode -- see MaxReduction_Temporal_Mesh_C.h) and is NOT auto-cleared
// between separate m5_cgra_run calls (the mesh persists, matching real
// hardware behavior). This workload therefore runs rounds in the SAME
// order as MaxReduction_Temporal_Top_C__TB.cpp (round 1, expected max=9,
// then round 2, expected max=100) -- do not reorder kRounds.
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static \
//               -o max_reduction8_cgra_gem5_workload_arm \
//               max_reduction8_cgra_gem5_workload.c m5ops_arm64/m5op.o

#include <stdio.h>
#include <stdint.h>

#define N 8

// Must match gem5/src/cgra/cgra_kernels.hh.
#define KERNEL_MAX_REDUCTION8_TEMPORAL 6
#define KERNEL_MAX_REDUCTION8_SPATIAL 7

typedef struct {
    int32_t v[8];
    int32_t total;
} CgraMaxReduction8Args;

extern uint64_t m5_cgra_run(uint64_t kernel_id, uint64_t args_ptr);

static int32_t max_reduction8_cgra(uint64_t kernel_id, const int32_t *v, uint64_t *cycles_out) {
    CgraMaxReduction8Args args;
    for (int i = 0; i < N; i++) args.v[i] = v[i];
    *cycles_out = m5_cgra_run(kernel_id, (uint64_t)(uintptr_t)&args);
    return args.total;
}

typedef struct {
    const char *label;
    int32_t v[N];
    int32_t expected_max;
} Round;

static const Round kRounds[] = {
    {"vector de referencia", {6, -2, 9, 4, 0, 7, -5, 3}, 9},
    {"vector con seed grande", {100, -55, 30, 7, -12, 4, -9, 15}, 100},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        uint64_t cyc_t, cyc_s;
        int32_t max_t = max_reduction8_cgra(KERNEL_MAX_REDUCTION8_TEMPORAL, kRounds[r].v, &cyc_t);
        int32_t max_s = max_reduction8_cgra(KERNEL_MAX_REDUCTION8_SPATIAL, kRounds[r].v, &cyc_s);

        int ok_t = (max_t == kRounds[r].expected_max);
        int ok_s = (max_s == kRounds[r].expected_max);
        all_ok = all_ok && ok_t && ok_s;

        printf("%s:\n", kRounds[r].label);
        printf("  temporal: max=%d expected=%d cycles=%llu -> %s\n",
               max_t, kRounds[r].expected_max, (unsigned long long)cyc_t, ok_t ? "PASS" : "FAIL");
        printf("  spatial : max=%d expected=%d cycles=%llu -> %s\n",
               max_s, kRounds[r].expected_max, (unsigned long long)cyc_s, ok_s ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
