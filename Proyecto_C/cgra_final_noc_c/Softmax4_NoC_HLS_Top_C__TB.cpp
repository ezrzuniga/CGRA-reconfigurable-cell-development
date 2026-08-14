// Softmax4_NoC_HLS_Top_C__TB.cpp
// Testbench plano para Softmax4_NoC_HLS_Top_C: 3 casos (misma semilla
// 20260810 y mismos logits que CGRA_Final_Softmax4_C__TB.cpp), validando SUM
// y e0 contra 2^x_i calculado con int64_t independiente de la malla. La
// division softmax_i = 2^x_i/SUM (parte escalar, fuera del array real) se
// imprime aca mismo, como referencia del contrato completo del acelerador.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "Softmax4_NoC_HLS_Top_C.h"

static const int32_t EXP2_SHIFT_BIAS = 9;

struct SoftmaxCase {
    std::string label;
    int32_t x[4];
};

static bool run_case(int case_num, const SoftmaxCase& tc) {
    int64_t numerator[4];
    int64_t sum_ref = 0;
    for (int i = 0; i < 4; i++) {
        numerator[i] = int64_t(1) << (tc.x[i] + EXP2_SHIFT_BIAS);
        sum_ref += numerator[i];
    }

    int32_t sum_got = 0, e0_got = 0;
    Softmax4_NoC_HLS_Top_C(tc.x[0], tc.x[1], tc.x[2], tc.x[3], &sum_got, &e0_got);

    bool ok = ((int64_t)sum_got == sum_ref) && ((int64_t)e0_got == numerator[0]);

    printf("CASO %d -- %s\n  x = [%d, %d, %d, %d]\n", case_num, tc.label.c_str(),
           tc.x[0], tc.x[1], tc.x[2], tc.x[3]);
    printf("  SUM esperado=%lld obtenido=%d ; e0 esperado=%lld obtenido=%d -> %s\n",
           (long long)sum_ref, sum_got, (long long)numerator[0], e0_got, ok ? "PASS" : "FAIL");

    printf("  softmax_2(x) = 2^x_i / SUM (division fuera de la malla):\n");
    for (int i = 0; i < 4; i++) {
        double s = double(numerator[i]) / double(sum_ref);
        printf("    x%d=%3d  2^x%d=%10lld  softmax_2=%.6f\n", i, tc.x[i], i, (long long)numerator[i], s);
    }
    printf("\n");
    return ok;
}

static void gen_random_case(int32_t x[4], int lo, int hi) {
    for (int i = 0; i < 4; i++) x[i] = lo + std::rand() % (hi - lo + 1);
}

int main() {
    printf("############################################################\n"
           "#  Softmax base-2 sobre Softmax4_NoC_HLS_Top_C (NoC 3x3)    #\n"
           "############################################################\n\n");

    std::srand(20260810);
    SoftmaxCase cases[3];
    cases[0].label = "logits iguales (softmax deberia dar 1/4 parejo)";
    cases[0].x[0] = 0; cases[0].x[1] = 0; cases[0].x[2] = 0; cases[0].x[3] = 0;
    cases[1].label = "un logit dominante";
    cases[1].x[0] = 0; cases[1].x[1] = 0; cases[1].x[2] = 6; cases[1].x[3] = 0;
    cases[2].label = "logits aleatorios con signo";
    gen_random_case(cases[2].x, -9, 9);

    bool all_ok = true;
    for (int c = 0; c < 3; c++) all_ok = run_case(c + 1, cases[c]) && all_ok;

    printf("============================================================\n");
    printf(all_ok ? "RESULTADO: PASS (los 3 casos)\n" : "RESULTADO: FAIL\n");
    printf("============================================================\n");
    return all_ok ? 0 : 1;
}
