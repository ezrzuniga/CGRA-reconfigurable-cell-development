// SumReduce8_NoC_HLS_Top_C__TB.cpp
// Testbench plano para SumReduce8_NoC_HLS_Top_C: 3 casos (misma semilla
// 20260810 y mismos vectores que CGRA_Final_SumReduce8_C__TB.cpp) contra
// TOTAL = sum(v[0..7]) calculado en C++ puro.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "SumReduce8_NoC_HLS_Top_C.h"

struct SumCase {
    std::string label;
    int32_t v[8];
};

static bool run_case(int case_num, const SumCase& tc) {
    int32_t expected = 0;
    for (int i = 0; i < 8; i++) expected += tc.v[i];

    int32_t got = 0;
    SumReduce8_NoC_HLS_Top_C(tc.v, &got);

    bool ok = (got == expected);
    printf("CASO %d -- %s\n  v = [", case_num, tc.label.c_str());
    for (int i = 0; i < 8; i++) printf("%d%s", tc.v[i], i < 7 ? ", " : "");
    printf("]\n  TOTAL esperado=%d obtenido=%d -> %s\n\n", expected, got, ok ? "PASS" : "FAIL");
    return ok;
}

static void gen_random_case(int32_t v[8], int lo, int hi) {
    for (int i = 0; i < 8; i++) v[i] = lo + std::rand() % (hi - lo + 1);
}

int main() {
    printf("############################################################\n"
           "#  Reduccion de suma (8 enteros) sobre SumReduce8_NoC_Top   #\n"
           "############################################################\n\n");

    std::srand(20260810);
    SumCase cases[3];
    cases[0].label = "unos y ceros (arbol facil de seguir a mano)";
    for (int i = 0; i < 8; i++) cases[0].v[i] = 1;
    cases[1].label = "potencias de dos (una contribucion por rama es identificable)";
    for (int i = 0; i < 8; i++) cases[1].v[i] = 1 << i;
    cases[2].label = "enteros aleatorios con signo";
    gen_random_case(cases[2].v, -20, 20);

    bool all_ok = true;
    for (int c = 0; c < 3; c++) all_ok = run_case(c + 1, cases[c]) && all_ok;

    printf("============================================================\n");
    printf(all_ok ? "RESULTADO: PASS (los 3 casos)\n" : "RESULTADO: FAIL\n");
    printf("============================================================\n");
    return all_ok ? 0 : 1;
}
