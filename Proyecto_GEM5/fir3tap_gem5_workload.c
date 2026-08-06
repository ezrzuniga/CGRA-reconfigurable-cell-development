// fir3tap_gem5_workload.c
// Baseline de CPU para un filtro FIR de 3 taps -- MISMOS casos de prueba
// que Proyecto_C/fir_hls_c, para comparar contra
// comparison_fir_convolution.md. Ver sum_reduction8_gem5_workload.c para el
// razonamiento (-O0, volatile).
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static -o fir3tap_gem5_workload_arm \
//               fir3tap_gem5_workload.c

#include <stdio.h>
#include <stdint.h>

#define TAPS 3
#define NOUT 4

static void fir3tap_kernel(volatile int32_t *w, volatile int32_t *x, int32_t *y) {
    for (int n = 0; n < NOUT; n++) {
        int32_t acc = 0;
        for (int k = 0; k < TAPS; k++) acc += w[k] * x[n + k];
        y[n] = acc;
    }
}

typedef struct {
    const char *label;
    int32_t w[TAPS];
    int32_t x[TAPS + NOUT - 1];
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
        volatile int32_t w[TAPS], x[TAPS + NOUT - 1];
        for (int i = 0; i < TAPS; i++) w[i] = kRounds[r].w[i];
        for (int i = 0; i < TAPS + NOUT - 1; i++) x[i] = kRounds[r].x[i];

        int32_t y[NOUT];
        fir3tap_kernel(w, x, y);

        int ok = 1;
        for (int n = 0; n < NOUT; n++)
            if (y[n] != kRounds[r].expected_y[n]) ok = 0;
        all_ok = all_ok && ok;

        printf("%s: y=[%d,%d,%d,%d] expected=[%d,%d,%d,%d] -> %s\n",
               kRounds[r].label, y[0], y[1], y[2], y[3],
               kRounds[r].expected_y[0], kRounds[r].expected_y[1],
               kRounds[r].expected_y[2], kRounds[r].expected_y[3],
               ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
