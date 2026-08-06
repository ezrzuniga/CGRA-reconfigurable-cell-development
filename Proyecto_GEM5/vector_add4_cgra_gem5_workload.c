// vector_add4_cgra_gem5_workload.c
// CGRA-offload counterpart to vector_add4_gem5_workload.c: same 2 test
// cases, same expected outputs, but c[] is computed by the real CGRA
// spatial (SIMD, VLEN=4) design via the m5_cgra_run pseudo-op instead of a
// plain C loop -- see gem5/src/cgra/cgra_kernels.{hh,cc} for the bridge
// (drives the actual Proyecto_C/vector_add_hls_c/VectorAdd_Spatial_Top_C.h
// model that was synthesized/cosim'd for comparison_vector_add.md).
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static \
//               -o vector_add4_cgra_gem5_workload_arm \
//               vector_add4_cgra_gem5_workload.c m5ops_arm64/m5op.o

#include <stdio.h>
#include <stdint.h>

#define N 4

// Must match gem5/src/cgra/cgra_kernels.hh: KERNEL_VECTOR_ADD4_SPATIAL / VectorAdd4Args.
#define KERNEL_VECTOR_ADD4_SPATIAL 1

typedef struct {
    int32_t a[4];
    int32_t b[4];
    int32_t c[4];
} CgraVectorAdd4Args;

extern uint64_t m5_cgra_run(uint64_t kernel_id, uint64_t args_ptr);

static void vector_add4_cgra_kernel(const int32_t *a, const int32_t *b, int32_t *c) {
    CgraVectorAdd4Args args;
    for (int n = 0; n < N; n++) { args.a[n] = a[n]; args.b[n] = b[n]; }
    uint64_t cycles = m5_cgra_run(KERNEL_VECTOR_ADD4_SPATIAL, (uint64_t)(uintptr_t)&args);
    for (int n = 0; n < N; n++) c[n] = args.c[n];
    printf("  (CGRA reported %llu cycles)\n", (unsigned long long)cycles);
}

typedef struct {
    const char *label;
    int32_t a[N];
    int32_t b[N];
    int32_t expected_c[N];
} Round;

static const Round kRounds[] = {
    {"positivos", {1, 2, 3, 4}, {10, 20, 30, 40}, {11, 22, 33, 44}},
    {"con negativos", {-5, 7, -3, 9}, {2, -8, 6, -1}, {-3, -1, 3, 8}},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        int32_t a[N], b[N];
        for (int i = 0; i < N; i++) { a[i] = kRounds[r].a[i]; b[i] = kRounds[r].b[i]; }

        int32_t c[N];
        vector_add4_cgra_kernel(a, b, c);

        int ok = 1;
        for (int n = 0; n < N; n++)
            if (c[n] != kRounds[r].expected_c[n]) ok = 0;
        all_ok = all_ok && ok;

        printf("%s: c=[%d,%d,%d,%d] expected=[%d,%d,%d,%d] -> %s\n",
               kRounds[r].label, c[0], c[1], c[2], c[3],
               kRounds[r].expected_c[0], kRounds[r].expected_c[1],
               kRounds[r].expected_c[2], kRounds[r].expected_c[3],
               ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
