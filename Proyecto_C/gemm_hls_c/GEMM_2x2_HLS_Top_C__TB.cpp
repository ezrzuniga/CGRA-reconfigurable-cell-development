// GEMM_2x2_HLS_Top_C__TB.cpp
// Testbench plano (sin sc_main/sc_clock/sc_signal) de GEMM_2x2_HLS_Top_C.
// A diferencia de la version anterior (que dejaba que el propio top cargara
// gemm_program_c() en cada corrida), aca el programa se sube UNA SOLA VEZ
// via la interfaz de programacion (prog_valid/prog_row/prog_col/prog_slot/
// prog_instr) y luego se dispara start=true dos veces, con operandos
// distintos, SIN volver a programar -- demuestra que el programa persiste
// en el `static GemmMesh_C mesh` del top entre invocaciones separadas,
// igual que hardware reconfigurable real (firmware cargado una vez, corrido
// muchas veces). Mismos dos casos de prueba y mismos valores esperados que
// la version anterior, para validar contra un resultado ya conocido.
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

    // -- 1) Programar la malla una unica vez ------------------------------
    GemmInstr_C prog[GEMM_ROWS][GEMM_COLS][GEMM_INSTR_MEM_SIZE];
    gemm_program_c(prog);

    GemmLink_C dummy_in_N[GEMM_NUM_PHASES][GEMM_COLS];
    GemmLink_C dummy_in_S[GEMM_NUM_PHASES][GEMM_COLS];
    GemmLink_C dummy_in_W[GEMM_NUM_PHASES][GEMM_ROWS];
    GemmLink_C dummy_in_E[GEMM_NUM_PHASES][GEMM_ROWS];
    GemmLink_C dummy_out_N[GEMM_COLS], dummy_out_S[GEMM_COLS];
    GemmLink_C dummy_out_W[GEMM_ROWS], dummy_out_E[GEMM_ROWS];

    int programmed = 0;
    const int total_instrs = GEMM_ROWS * GEMM_COLS * GEMM_INSTR_MEM_SIZE;
    for (int r = 0; r < GEMM_ROWS; r++) {
        for (int c = 0; c < GEMM_COLS; c++) {
            for (int slot = 0; slot < GEMM_INSTR_MEM_SIZE; slot++) {
                bool done = false;
                GEMM_2x2_HLS_Top_C(
                    /*prog_valid=*/true, r, c, slot, prog[r][c][slot],
                    /*start=*/false, done,
                    dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                    dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
                if (done) programmed++;
            }
        }
    }
    check(ok, "programa espacial cargado", "prog_valid x" + std::to_string(total_instrs),
          total_instrs, programmed);

    // -- 2) Correr dos casos de prueba SIN volver a programar -------------
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

        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);
        std::printf("A=[[%d,%d],[%d,%d]] B=[[%d,%d],[%d,%d]] C esperado=[[%d,%d],[%d,%d]]\n",
                    tc.A[0][0], tc.A[0][1], tc.A[1][0], tc.A[1][1],
                    tc.B[0][0], tc.B[0][1], tc.B[1][0], tc.B[1][1],
                    C[0][0], C[0][1], C[1][0], C[1][1]);

        // A entra por in_W[fase][fila] = A[fila][fase] (columna `fase` de A);
        // B entra por in_N[fase][columna] = B[fase][columna] (fila `fase` de B).
        // in_S/in_E no los usa el programa de GEMM -- quedan en cero.
        GemmLink_C in_N[GEMM_NUM_PHASES][GEMM_COLS];
        GemmLink_C in_S[GEMM_NUM_PHASES][GEMM_COLS];
        GemmLink_C in_W[GEMM_NUM_PHASES][GEMM_ROWS];
        GemmLink_C in_E[GEMM_NUM_PHASES][GEMM_ROWS];
        for (int phase = 0; phase < GEMM_NUM_PHASES; phase++) {
            for (int row = 0; row < GEMM_ROWS; row++) in_W[phase][row][0] = tc.A[row][phase];
            for (int col = 0; col < GEMM_COLS; col++) in_N[phase][col][0] = tc.B[phase][col];
        }

        GemmLink_C out_N[GEMM_COLS], out_S[GEMM_COLS];
        GemmLink_C out_W[GEMM_ROWS], out_E[GEMM_ROWS];
        bool done = false;
        GemmInstr_C unused_instr; // prog_valid=false -- nunca se lee
        GEMM_2x2_HLS_Top_C(
            /*prog_valid=*/false, 0, 0, 0, unused_instr,
            /*start=*/true, done,
            in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
        check(ok, "C[0][0]", tc.label, C[0][0], out_W[0][0].to_int());
        check(ok, "C[0][1]", tc.label, C[0][1], out_E[0][0].to_int());
        check(ok, "C[1][0]", tc.label, C[1][0], out_W[1][0].to_int());
        check(ok, "C[1][1]", tc.label, C[1][1], out_E[1][0].to_int());
    }

    if (ok) {
        std::printf("\nPASS: GEMM_2x2_HLS_Top_C (template cgra_run + malla real, C/C++ puro) "
                    "resuelve GEMM 2x2 en ambos casos, programando la malla una unica vez.\n");
    }
    return ok ? 0 : 1;
}
