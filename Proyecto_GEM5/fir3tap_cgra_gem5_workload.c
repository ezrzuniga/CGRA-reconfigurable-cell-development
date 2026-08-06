// fir3tap_cgra_gem5_workload.c
// CGRA-offload counterpart to fir3tap_gem5_workload.c: same 2 test cases,
// but y[] is computed by the real CGRA temporal AND spatial designs via
// m5_cgra_run -- see gem5/src/cgra/cgra_kernels.{hh,cc} for the bridge
// (drives the actual Proyecto_C/fir_hls_c/ models synthesized/cosim'd for
// comparison_fir_convolution.md).
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static \
//               -o fir3tap_cgra_gem5_workload_arm \
//               fir3tap_cgra_gem5_workload.c m5ops_arm64/m5op.o

#include <stdio.h>
#include <stdint.h>

#define TAPS 3
#define NOUT 4

// Must match gem5/src/cgra/cgra_kernels.hh.
#define KERNEL_FIR3TAP_TEMPORAL 8
#define KERNEL_FIR3TAP_SPATIAL 9

typedef struct {
    int32_t w[3];
    int32_t x[6];
    int32_t y[4];
} CgraFir3TapArgs;

extern uint64_t m5_cgra_run(uint64_t kernel_id, uint64_t args_ptr);

static void fir3tap_cgra(uint64_t kernel_id, const int32_t *w, const int32_t *x, int32_t *y, uint64_t *cycles_out) {
    CgraFir3TapArgs args;
    for (int i = 0; i < TAPS; i++) args.w[i] = w[i];
    for (int i = 0; i < 6; i++) args.x[i] = x[i];
    *cycles_out = m5_cgra_run(kernel_id, (uint64_t)(uintptr_t)&args);
    for (int i = 0; i < NOUT; i++) y[i] = args.y[i];
}

typedef struct {
    const char *label;
    int32_t w[TAPS];
    int32_t x[6];
    int32_t expected_y[NOUT];
} Round;

static const Round kRounds[] = {
    {"pesos/muestras positivos", {1, 2, 3}, {1, 2, 3, 4, 5, 6}, {14, 20, 26, 32}},
    {"con valores negativos", {2, -1, 3}, {5, -2, 4, 1, -3, 6}, {24, -5, -2, 23}},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        uint64_t cyc_t, cyc_s;
        int32_t y_t[NOUT], y_s[NOUT];
        fir3tap_cgra(KERNEL_FIR3TAP_TEMPORAL, kRounds[r].w, kRounds[r].x, y_t, &cyc_t);
        fir3tap_cgra(KERNEL_FIR3TAP_SPATIAL, kRounds[r].w, kRounds[r].x, y_s, &cyc_s);

        int ok_t = 1, ok_s = 1;
        for (int n = 0; n < NOUT; n++) {
            if (y_t[n] != kRounds[r].expected_y[n]) ok_t = 0;
            if (y_s[n] != kRounds[r].expected_y[n]) ok_s = 0;
        }
        all_ok = all_ok && ok_t && ok_s;

        printf("%s:\n", kRounds[r].label);
        printf("  temporal: y=[%d,%d,%d,%d] expected=[%d,%d,%d,%d] cycles=%llu -> %s\n",
               y_t[0], y_t[1], y_t[2], y_t[3],
               kRounds[r].expected_y[0], kRounds[r].expected_y[1], kRounds[r].expected_y[2], kRounds[r].expected_y[3],
               (unsigned long long)cyc_t, ok_t ? "PASS" : "FAIL");
        printf("  spatial : y=[%d,%d,%d,%d] expected=[%d,%d,%d,%d] cycles=%llu -> %s\n",
               y_s[0], y_s[1], y_s[2], y_s[3],
               kRounds[r].expected_y[0], kRounds[r].expected_y[1], kRounds[r].expected_y[2], kRounds[r].expected_y[3],
               (unsigned long long)cyc_s, ok_s ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
