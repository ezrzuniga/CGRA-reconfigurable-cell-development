// max_reduction8_gem5_workload.c
// Baseline de CPU para reduccion por maximo de 8 elementos -- MISMOS
// vectores que Proyecto_C/max_reduction_hls_c, para comparar contra
// comparison_max_reduction.md. Ver sum_reduction8_gem5_workload.c para el
// razonamiento (-O0, volatile). A diferencia de la CGRA (que no tiene
// opcode MAX nativo y necesita 4 instrucciones encadenadas por
// combinacion), la CPU SI tiene una comparacion condicional nativa -- este
// baseline usa el operador ternario normal de C, sin ninguna restriccion
// artificial, porque el punto de este baseline es medir "como le va a una
// CPU convencional", no replicar la limitacion de ISA de la CGRA.

#include <stdio.h>
#include <stdint.h>

#define N 8

static int32_t max_reduction8_kernel(volatile int32_t *v) {
    int32_t m = v[0];
    for (int i = 1; i < N; i++) {
        m = (v[i] > m) ? v[i] : m;
    }
    return m;
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
        volatile int32_t v[N];
        for (int i = 0; i < N; i++) v[i] = kRounds[r].v[i];

        int32_t m = max_reduction8_kernel(v);
        int ok = (m == kRounds[r].expected_max);
        all_ok = all_ok && ok;

        printf("%s: max=%d expected=%d -> %s\n",
               kRounds[r].label, m, kRounds[r].expected_max, ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
