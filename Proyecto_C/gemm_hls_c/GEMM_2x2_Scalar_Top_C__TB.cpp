// GEMM_2x2_Scalar_Top_C__TB.cpp
// Transliteracion a C/C++ puro de gemm_hls/GEMM_2x2_HLS_Top__TB.cpp: ejercita
// el top de funcion fija por su limite de puertos escalares (a00..b11 ->
// c00..c11 + start/done), sin conocer nada de la malla, del programa espacial
// ni de las fases -- exactamente lo que veria un host detras de un AXI-Lite.
//
// Mismos dos casos de prueba y mismos valores esperados que el original.
// Se agrega un tercer chequeo que el original no hacia: repetir el caso 1
// DESPUES del caso 2, para confirmar que cada invocacion es independiente del
// historial (los acumuladores se limpian solos, ver cgra_run).

#include <cstdio>
#include <cstdint>
#include <string>
#include "GEMM_2x2_Scalar_Top_C.h"

struct GemmCase {
    const char* label;
    int32_t A[2][2];
    int32_t B[2][2];
};

static void check(bool& ok, const char* label, const std::string& inputs, int32_t expected, int32_t got) {
    bool pass = (expected == got);
    printf("%s %s\n  entrada  : %s\n  esperado : %d\n  obtenido : %d\n",
           pass ? "PASS" : "FAIL", label, inputs.c_str(), expected, got);
    if (!pass) ok = false;
}

static bool run_case(const GemmCase& tc) {
    int32_t C[2][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += tc.A[i][k] * tc.B[k][j];
            C[i][j] = sum;
        }

    printf("\n==== Caso: %s ====\n", tc.label);
    printf("A=[[%d,%d],[%d,%d]] B=[[%d,%d],[%d,%d]] C esperado=[[%d,%d],[%d,%d]]\n",
           tc.A[0][0], tc.A[0][1], tc.A[1][0], tc.A[1][1],
           tc.B[0][0], tc.B[0][1], tc.B[1][0], tc.B[1][1],
           C[0][0], C[0][1], C[1][0], C[1][1]);

    bool done = false;
    ap_int<GEMM_DATA_W> c00, c01, c10, c11;
    GEMM_2x2_Scalar_Top_C(/*start=*/true, done,
                          tc.A[0][0], tc.A[0][1], tc.A[1][0], tc.A[1][1],
                          tc.B[0][0], tc.B[0][1], tc.B[1][0], tc.B[1][1],
                          c00, c01, c10, c11);

    bool ok = true;
    check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
    check(ok, "C[0][0]", tc.label, C[0][0], c00.to_int());
    check(ok, "C[0][1]", tc.label, C[0][1], c01.to_int());
    check(ok, "C[1][0]", tc.label, C[1][0], c10.to_int());
    check(ok, "C[1][1]", tc.label, C[1][1], c11.to_int());
    return ok;
}

int main() {
    bool ok = true;

    // start=false no debe producir nada (mismo contrato que el IDLE del top
    // SystemC: sin start, done queda en 0 y la malla no corre).
    {
        bool done = true;
        ap_int<GEMM_DATA_W> c00, c01, c10, c11;
        GEMM_2x2_Scalar_Top_C(/*start=*/false, done, 1, 1, 1, 1, 1, 1, 1, 1, c00, c01, c10, c11);
        check(ok, "sin start, done queda en 0", "start=false", 0, done ? 1 : 0);
    }

    GemmCase cases[2] = {
        {"enteros positivos",   {{1, 2}, {3, 4}},   {{5, 6}, {7, 8}}},
        {"con valores negativos", {{-3, 5}, {2, -4}}, {{6, -1}, {-2, 3}}},
    };

    ok = run_case(cases[0]) && ok;
    ok = run_case(cases[1]) && ok;

    // Independencia del historial: el caso 1 repetido tiene que dar lo mismo
    // que la primera vez, sin ninguna limpieza explicita desde el host.
    printf("\n---- Repeticion del caso 1 (independencia del historial) ----\n");
    ok = run_case(cases[0]) && ok;

    if (ok) {
        printf("\nPASS: GEMM_2x2_Scalar_Top_C resuelve GEMM 2x2 por su limite de puertos "
               "escalares, y cada invocacion es independiente de las anteriores.\n");
    }
    return ok ? 0 : 1;
}
