// GEMM_2x2_HLS_Top_C__TB.cpp
// Testbench plano (sin sc_main/sc_clock/sc_signal) de GEMM_2x2_HLS_Top_C:
// llama la funcion top directamente con start=true y compara c00..c11 contra
// el resultado esperado. Mismos dos casos de prueba que
// gemm_hls/GEMM_2x2_HLS_Top__TB.cpp (mismos valores de A/B/C esperados), para
// poder validar la migracion contra un resultado ya conocido.
#include <cstdint>
#include <cstdio>
#include <string>
#include "GEMM_2x2_HLS_Top_C.h"

struct GemmCase {
    const char* label;
    int32_t A[2][2];
    int32_t B[2][2];
};

static bool check(bool& ok, const std::string& label, const std::string& inputs,
                   int32_t expected, int32_t got) {
    bool pass = (got == expected);
    std::printf("%s %s\n  entrada  : %s\n  esperado : %d\n  obtenido : %d\n",
                pass ? "PASS" : "FAIL", label.c_str(), inputs.c_str(), expected, got);
    if (!pass) ok = false;
    return pass;
}

int main() {
    bool ok = true;

    GemmCase cases[2] = {
        {"enteros positivos",
         {{1, 2}, {3, 4}},
         {{5, 6}, {7, 8}}},
        {"con valores negativos",
         {{-3, 5}, {2, -4}},
         {{6, -1}, {-2, 3}}},
    };

    for (const GemmCase& tc : cases) {
        int32_t C[2][2];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                int32_t sum = 0;
                for (int k = 0; k < 2; k++) sum += tc.A[i][k] * tc.B[k][j];
                C[i][j] = sum;
            }

        std::printf("\n==== Caso: %s ====\n", tc.label);
        std::printf("A=[[%d,%d],[%d,%d]] B=[[%d,%d],[%d,%d]] C esperado=[[%d,%d],[%d,%d]]\n",
                    tc.A[0][0], tc.A[0][1], tc.A[1][0], tc.A[1][1],
                    tc.B[0][0], tc.B[0][1], tc.B[1][0], tc.B[1][1],
                    C[0][0], C[0][1], C[1][0], C[1][1]);

        bool done = false;
        ap_int<32> c00, c01, c10, c11;
        GEMM_2x2_HLS_Top_C(
            /*start=*/true, done,
            tc.A[0][0], tc.A[0][1], tc.A[1][0], tc.A[1][1],
            tc.B[0][0], tc.B[0][1], tc.B[1][0], tc.B[1][1],
            c00, c01, c10, c11);

        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
        check(ok, "C[0][0]", tc.label, C[0][0], c00.to_int());
        check(ok, "C[0][1]", tc.label, C[0][1], c01.to_int());
        check(ok, "C[1][0]", tc.label, C[1][0], c10.to_int());
        check(ok, "C[1][1]", tc.label, C[1][1], c11.to_int());
    }

    if (ok) {
        std::printf("\nPASS: GEMM_2x2_HLS_Top_C (FSM + malla real, C/C++ puro) resuelve GEMM 2x2 "
                    "en ambos casos de prueba.\n");
    }
    return ok ? 0 : 1;
}
