// sum_reduction16_gem5_workload.c
// Baseline de CPU para reduccion por suma de 16 elementos -- MISMOS
// vectores que Proyecto_C/sum_reduction16_hls_c, para comparar contra
// comparison_sum_reduction_n16.md. Ver sum_reduction8_gem5_workload.c para
// el razonamiento completo (mismo patron, -O0, volatile).
//
// Compilar: aarch64-linux-gnu-gcc -O0 -static -o sum_reduction16_gem5_workload_arm \
//               sum_reduction16_gem5_workload.c

#include <stdio.h>
#include <stdint.h>

#define N 16

static int32_t sum_reduction16_kernel(volatile int32_t *v) {
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
    {"v16a", {6, -2, 9, 4, 0, 7, -5, 3, 2, -8, 6, 1, -3, 5, 4, -1}, 28},
    {"v16b", {10, 20, -5, 3, 7, -2, 8, 1, 4, -6, 9, 0, 2, -1, 5, 3}, 58},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        volatile int32_t v[N];
        for (int i = 0; i < N; i++) v[i] = kRounds[r].v[i];

        int32_t total = sum_reduction16_kernel(v);
        int ok = (total == kRounds[r].expected_total);
        all_ok = all_ok && ok;

        printf("%s: total=%d expected=%d -> %s\n",
               kRounds[r].label, total, kRounds[r].expected_total, ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
