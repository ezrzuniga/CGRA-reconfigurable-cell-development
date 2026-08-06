// sum_reduction8_gem5_workload.c
// Baseline de CPU (sin CGRA) para reduccion por suma de 8 elementos --
// MISMOS vectores de prueba que Proyecto_C/sum_reduction_hls_c (n=8), para
// que los ciclos que reporte gem5 (numCycles en stats.txt) sean
// directamente comparables contra los ciclos reales medidos en la CGRA
// (ver comparison_sum_reduction.md, y comparison_cpu_baseline.md para la
// comparacion de 3 vias CPU/temporal/espacial).
//
// -O0 a proposito (mismo razonamiento que sum_reduction_workload.c): evita
// que el compilador colapse el loop de reduccion a una constante, para que
// gem5 mida "ejecutar el kernel real", no "ejecutar un resultado
// precomputado".
//
// Compilar (ARM64 estatico, para correr bajo gem5 build/ARM):
//   aarch64-linux-gnu-gcc -O0 -static -o sum_reduction8_gem5_workload_arm \
//       sum_reduction8_gem5_workload.c

#include <stdio.h>
#include <stdint.h>

#define N 8

static int32_t sum_reduction8_kernel(volatile int32_t *v) {
    int32_t total = 0;
    for (int i = 0; i < N; i++) {
        total += v[i];
    }
    return total;
}

typedef struct {
    const char *label;
    int32_t v[N];
    int32_t expected_total;
} Round;

static const Round kRounds[] = {
    {"vector de referencia", {6, -2, 9, 4, 0, 7, -5, 3}, 22},
    {"vector con seed grande", {100, -55, 30, 7, -12, 4, -9, 15}, 80},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        volatile int32_t v[N];
        for (int i = 0; i < N; i++) v[i] = kRounds[r].v[i];

        int32_t total = sum_reduction8_kernel(v);
        int ok = (total == kRounds[r].expected_total);
        all_ok = all_ok && ok;

        printf("%s: total=%d expected=%d -> %s\n",
               kRounds[r].label, total, kRounds[r].expected_total, ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
