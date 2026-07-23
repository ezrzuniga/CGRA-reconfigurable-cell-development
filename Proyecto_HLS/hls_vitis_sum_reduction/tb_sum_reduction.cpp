// tb_sum_reduction.cpp
// C testbench for sum_reduction_kernel. Golden (seed, v, expected_total)
// triples are taken verbatim from the three rounds run by
// ../mesh_wrapper/MeshWrapper_SumReduction__TB.cpp (FakeCsrDma::run), which
// exercises the same total = seed + sum(v[0..6]) contract end-to-end through
// the CGRA mesh (Enrutamiento+Memoria+Escalar+Vectorial). That TB is the
// architectural golden reference; this one only checks the arithmetic this
// synthesizable kernel is allowed to reproduce.

#include <cstdio>

#include "sum_reduction_kernel.h"

struct Round {
    const char *label;
    int32_t seed;
    int32_t v[SUM_REDUCTION_VECTOR_LEN];
    int32_t expected_total;
};

static const Round kRounds[] = {
    {"Ronda 1", 100, {6, -2, 9, 4, 0, 7, -5}, 119},
    {"Ronda 2", 0,   {1, 2, 3, 4, 5, 6, 7},   28},
    {"Ronda 3", -50, {10, -20, 30, -40, 50, -60, 70}, -10},
};

int main() {
    bool all_ok = true;

    for (const Round &r : kRounds) {
        int32_t v[SUM_REDUCTION_VECTOR_LEN];
        for (int i = 0; i < SUM_REDUCTION_VECTOR_LEN; i++) v[i] = r.v[i];

        int32_t out = 0;
        sum_reduction_kernel(r.seed, v, &out);

        bool ok = (out == r.expected_total);
        all_ok &= ok;

        printf("%s: seed=%d total=%d expected=%d -> %s\n",
               r.label, r.seed, out, r.expected_total, ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
