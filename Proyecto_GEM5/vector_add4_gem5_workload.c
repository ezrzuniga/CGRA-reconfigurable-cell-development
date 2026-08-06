// vector_add4_gem5_workload.c
// Baseline de CPU para suma de vectores de 4 elementos -- MISMOS casos de
// prueba que Proyecto_C/vector_add_hls_c, para comparar contra
// comparison_vector_add.md. Ver sum_reduction8_gem5_workload.c para el
// razonamiento (-O0, volatile).
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static -o vector_add4_gem5_workload_arm \
//               vector_add4_gem5_workload.c

#include <stdio.h>
#include <stdint.h>

#define N 4

static void vector_add4_kernel(volatile int32_t *a, volatile int32_t *b, int32_t *c) {
    for (int n = 0; n < N; n++) c[n] = a[n] + b[n];
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
        volatile int32_t a[N], b[N];
        for (int i = 0; i < N; i++) { a[i] = kRounds[r].a[i]; b[i] = kRounds[r].b[i]; }

        int32_t c[N];
        vector_add4_kernel(a, b, c);

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
