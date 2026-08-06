// MaxReduction_Spatial_Top_C__TB.cpp
// Programa las 4 celdas una vez (80 llamadas prog_valid: 4 celdas x 20
// slots), despues una sola invocacion start=true calcula el maximo del
// arbol. Mismos 2 vectores de referencia que MaxReduction_Temporal_Top_C__TB.cpp.
#include <cstdint>
#include <cstdio>
#include <string>
#include "MaxReduction_Spatial_Top_C.h"

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

    MaxRedSpatialInstr_C prog[MAXRED_S_ROWS][MAXRED_S_COLS][MAXRED_S_INSTR_MEM_SIZE];
    maxred_spatial_program_c(prog);

    MaxRedSpatialLink_C dummy_in_N[MAXRED_S_NUM_PHASES][MAXRED_S_COLS];
    MaxRedSpatialLink_C dummy_in_S[MAXRED_S_NUM_PHASES][MAXRED_S_COLS];
    MaxRedSpatialLink_C dummy_in_W[MAXRED_S_NUM_PHASES][MAXRED_S_ROWS];
    MaxRedSpatialLink_C dummy_in_E[MAXRED_S_NUM_PHASES][MAXRED_S_ROWS];
    MaxRedSpatialLink_C dummy_out_N[MAXRED_S_COLS], dummy_out_S[MAXRED_S_COLS];
    MaxRedSpatialLink_C dummy_out_W[MAXRED_S_ROWS], dummy_out_E[MAXRED_S_ROWS];

    int programmed = 0;
    const int total_instrs = MAXRED_S_ROWS * MAXRED_S_COLS * MAXRED_S_INSTR_MEM_SIZE;
    for (int r = 0; r < MAXRED_S_ROWS; r++) {
        for (int c = 0; c < MAXRED_S_COLS; c++) {
            for (int slot = 0; slot < MAXRED_S_INSTR_MEM_SIZE; slot++) {
                bool done = false;
                MaxReduction_Spatial_Top_C(true, r, c, slot, prog[r][c][slot], false, done,
                                            dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                                            dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
                if (done) programmed++;
            }
        }
    }
    check(ok, "programa espacial (arbol) cargado", "prog_valid x" + std::to_string(total_instrs),
          total_instrs, programmed);

    struct Case { const char* label; int32_t v[8]; int32_t expected_max; };
    Case cases[2] = {
        {"vector de referencia", {6, -2, 9, 4, 0, 7, -5, 3}, 9},
        {"vector con seed grande", {100, -55, 30, 7, -12, 4, -9, 15}, 100},
    };

    for (const Case& tc : cases) {
        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);

        // Misma asignacion de puertos externos que
        // sum_reduction_hls_c/SumReduction_Spatial_Top_C__TB.cpp:
        //   P00.N=v0 P00.W=v1   P01.N=v2 P01.E=v3
        //   P10.S=v4 P10.W=v5   P11.S=v6 P11.E=v7
        MaxRedSpatialLink_C in_N[MAXRED_S_NUM_PHASES][MAXRED_S_COLS];
        MaxRedSpatialLink_C in_S[MAXRED_S_NUM_PHASES][MAXRED_S_COLS];
        MaxRedSpatialLink_C in_W[MAXRED_S_NUM_PHASES][MAXRED_S_ROWS];
        MaxRedSpatialLink_C in_E[MAXRED_S_NUM_PHASES][MAXRED_S_ROWS];
        in_N[0][0][0] = tc.v[0]; in_W[0][0][0] = tc.v[1];
        in_N[0][1][0] = tc.v[2]; in_E[0][0][0] = tc.v[3];
        in_S[0][0][0] = tc.v[4]; in_W[0][1][0] = tc.v[5];
        in_S[0][1][0] = tc.v[6]; in_E[0][1][0] = tc.v[7];

        MaxRedSpatialLink_C out_N[MAXRED_S_COLS], out_S[MAXRED_S_COLS];
        MaxRedSpatialLink_C out_W[MAXRED_S_ROWS], out_E[MAXRED_S_ROWS];
        bool done = false;
        MaxRedSpatialInstr_C unused_instr;
        MaxReduction_Spatial_Top_C(false, 0, 0, 0, unused_instr, true, done,
                                    in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
        // Resultado final sale por el borde este externo de P11 (fila 1).
        check(ok, "maximo (espacial, 4 celdas x arbol de 3 niveles)", tc.label,
              tc.expected_max, out_E[1][0].to_int());
    }

    if (ok) {
        std::printf("\nPASS: MaxReduction_Spatial_Top_C (4 celdas, 1 fase, "
                    "arbol de maximos en paralelo) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
