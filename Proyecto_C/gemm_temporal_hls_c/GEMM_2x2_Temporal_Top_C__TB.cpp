// GEMM_2x2_Temporal_Top_C__TB.cpp
// Testbench plano (sin sc_main) de GEMM_2x2_Temporal_Top_C: programa la
// unica celda una unica vez (3 llamadas prog_valid), despues calcula cada
// C[i][j] con una invocacion start=true separada (4 por caso), sin volver a
// programar entre ellas ni entre los 2 casos -- mismo patron de "programar
// una vez, correr muchas" que GEMM_2x2_HLS_Top_C__TB.cpp y
// SumReduction_Temporal_Top_C__TB.cpp. Mismos 2 casos de prueba (mismos
// valores de A/B/C esperados) que GEMM_2x2_HLS_Top_C__TB.cpp, para que los
// resultados -- y, corridos por Vitis HLS, los ciclos -- sean directamente
// comparables entre el mapeo espacial y este temporal.
#include <cstdint>
#include <cstdio>
#include <string>
#include "GEMM_2x2_Temporal_Top_C.h"

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

    // -- 1) Programar la unica celda una unica vez --------------------------
    GemmTemporalInstr_C prog[GEMM_T_ROWS][GEMM_T_COLS][GEMM_T_INSTR_MEM_SIZE];
    gemm_temporal_program_c(prog);

    GemmTemporalLink_C dummy_in_N[GEMM_T_NUM_PHASES][GEMM_T_COLS];
    GemmTemporalLink_C dummy_in_S[GEMM_T_NUM_PHASES][GEMM_T_COLS];
    GemmTemporalLink_C dummy_in_W[GEMM_T_NUM_PHASES][GEMM_T_ROWS];
    GemmTemporalLink_C dummy_in_E[GEMM_T_NUM_PHASES][GEMM_T_ROWS];
    GemmTemporalLink_C dummy_out_N[GEMM_T_COLS], dummy_out_S[GEMM_T_COLS];
    GemmTemporalLink_C dummy_out_W[GEMM_T_ROWS], dummy_out_E[GEMM_T_ROWS];

    int programmed = 0;
    for (int slot = 0; slot < GEMM_T_INSTR_MEM_SIZE; slot++) {
        bool done = false;
        GEMM_2x2_Temporal_Top_C(
            /*prog_valid=*/true, 0, 0, slot, prog[0][0][slot],
            /*start=*/false, done,
            dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
            dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
        if (done) programmed++;
    }
    check(ok, "programa temporal cargado", "prog_valid x" + std::to_string(GEMM_T_INSTR_MEM_SIZE),
          GEMM_T_INSTR_MEM_SIZE, programmed);

    // -- 2) Correr los 2 casos, 4 invocaciones start=true por caso (una por
    //       C[i][j]), SIN volver a programar -----------------------------
    GemmCase cases[2] = {
        {"enteros positivos (mismo caso que GEMM espacial)",
         {{1, 2}, {3, 4}},
         {{5, 6}, {7, 8}}},
        {"con valores negativos (mismo caso que GEMM espacial)",
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

        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);
        std::printf("A=[[%d,%d],[%d,%d]] B=[[%d,%d],[%d,%d]] C esperado=[[%d,%d],[%d,%d]]\n",
                    tc.A[0][0], tc.A[0][1], tc.A[1][0], tc.A[1][1],
                    tc.B[0][0], tc.B[0][1], tc.B[1][0], tc.B[1][1],
                    C[0][0], C[0][1], C[1][0], C[1][1]);

        int32_t got[2][2];
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                // Fase k: A[i][k] por in_N, B[k][j] por in_S -- una
                // invocacion start=true completa por posicion de salida.
                GemmTemporalLink_C in_N[GEMM_T_NUM_PHASES][GEMM_T_COLS];
                GemmTemporalLink_C in_S[GEMM_T_NUM_PHASES][GEMM_T_COLS];
                GemmTemporalLink_C in_W[GEMM_T_NUM_PHASES][GEMM_T_ROWS];
                GemmTemporalLink_C in_E[GEMM_T_NUM_PHASES][GEMM_T_ROWS];
                for (int k = 0; k < GEMM_T_NUM_PHASES; k++) {
                    in_N[k][0][0] = tc.A[i][k];
                    in_S[k][0][0] = tc.B[k][j];
                }

                GemmTemporalLink_C out_N[GEMM_T_COLS], out_S[GEMM_T_COLS];
                GemmTemporalLink_C out_W[GEMM_T_ROWS], out_E[GEMM_T_ROWS];
                bool done = false;
                GemmTemporalInstr_C unused_instr;
                GEMM_2x2_Temporal_Top_C(
                    /*prog_valid=*/false, 0, 0, 0, unused_instr,
                    /*start=*/true, done,
                    in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

                check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
                got[i][j] = out_E[0][0].to_int();
            }
        }

        check(ok, "C[0][0]", tc.label, C[0][0], got[0][0]);
        check(ok, "C[0][1]", tc.label, C[0][1], got[0][1]);
        check(ok, "C[1][0]", tc.label, C[1][0], got[1][0]);
        check(ok, "C[1][1]", tc.label, C[1][1], got[1][1]);
    }

    if (ok) {
        std::printf("\nPASS: GEMM_2x2_Temporal_Top_C (1 celda, 4 invocaciones start=true, "
                    "una por C[i][j]) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
