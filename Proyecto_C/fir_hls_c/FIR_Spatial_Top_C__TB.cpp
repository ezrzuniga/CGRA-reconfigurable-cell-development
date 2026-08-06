// FIR_Spatial_Top_C__TB.cpp
// Programa las 4 celdas una vez (12 llamadas prog_valid), despues una sola
// invocacion start=true calcula las 4 muestras de salida en paralelo.
// Mismos 2 casos de prueba que FIR_Temporal_Top_C__TB.cpp.
#include <cstdint>
#include <cstdio>
#include <string>
#include "FIR_Spatial_Top_C.h"

struct FirCase {
    const char* label;
    int32_t w[FIR_S_NUM_PHASES];
    int32_t x[FIR_S_NUM_PHASES + 3];
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

    FirSpatialInstr_C prog[FIR_S_ROWS][FIR_S_COLS][FIR_S_INSTR_MEM_SIZE];
    fir_spatial_program_c(prog);

    FirSpatialLink_C dummy_in_N[FIR_S_NUM_PHASES][FIR_S_COLS];
    FirSpatialLink_C dummy_in_S[FIR_S_NUM_PHASES][FIR_S_COLS];
    FirSpatialLink_C dummy_in_W[FIR_S_NUM_PHASES][FIR_S_ROWS];
    FirSpatialLink_C dummy_in_E[FIR_S_NUM_PHASES][FIR_S_ROWS];
    FirSpatialLink_C dummy_out_N[FIR_S_COLS], dummy_out_S[FIR_S_COLS];
    FirSpatialLink_C dummy_out_W[FIR_S_ROWS], dummy_out_E[FIR_S_ROWS];

    int programmed = 0;
    const int total_instrs = FIR_S_ROWS * FIR_S_COLS * FIR_S_INSTR_MEM_SIZE;
    for (int r = 0; r < FIR_S_ROWS; r++) {
        for (int c = 0; c < FIR_S_COLS; c++) {
            for (int slot = 0; slot < FIR_S_INSTR_MEM_SIZE; slot++) {
                bool done = false;
                FIR_Spatial_Top_C(true, r, c, slot, prog[r][c][slot], false, done,
                                   dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                                   dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
                if (done) programmed++;
            }
        }
    }
    check(ok, "programa espacial cargado", "prog_valid x" + std::to_string(total_instrs),
          total_instrs, programmed);

    FirCase cases[2] = {
        {"pesos/muestras positivos", {1, 2, 3}, {1, 2, 3, 4, 5, 6}},
        {"con valores negativos", {2, -1, 3}, {5, -2, 4, 1, -3, 6}},
    };

    for (const FirCase& tc : cases) {
        int32_t y[4];
        for (int n = 0; n < 4; n++) {
            int32_t acc = 0;
            for (int k = 0; k < FIR_S_NUM_PHASES; k++) acc += tc.w[k] * tc.x[n + k];
            y[n] = acc;
        }

        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);
        std::printf("w=[%d,%d,%d] x=[%d,%d,%d,%d,%d,%d] y esperado=[%d,%d,%d,%d]\n",
                    tc.w[0], tc.w[1], tc.w[2], tc.x[0], tc.x[1], tc.x[2], tc.x[3], tc.x[4], tc.x[5],
                    y[0], y[1], y[2], y[3]);

        // Fase k: peso w[k] entra por in_N (broadcast a las 4 columnas),
        // x[n+k] entra por in_S de la columna n (ventana ya desplazada).
        FirSpatialLink_C in_N[FIR_S_NUM_PHASES][FIR_S_COLS];
        FirSpatialLink_C in_S[FIR_S_NUM_PHASES][FIR_S_COLS];
        FirSpatialLink_C in_W[FIR_S_NUM_PHASES][FIR_S_ROWS];
        FirSpatialLink_C in_E[FIR_S_NUM_PHASES][FIR_S_ROWS];
        for (int k = 0; k < FIR_S_NUM_PHASES; k++) {
            for (int c = 0; c < FIR_S_COLS; c++) {
                in_N[k][c][0] = tc.w[k];
                in_S[k][c][0] = tc.x[c + k];
            }
        }

        FirSpatialLink_C out_N[FIR_S_COLS], out_S[FIR_S_COLS];
        FirSpatialLink_C out_W[FIR_S_ROWS], out_E[FIR_S_ROWS];
        bool done = false;
        FirSpatialInstr_C unused_instr;
        FIR_Spatial_Top_C(false, 0, 0, 0, unused_instr, true, done,
                           in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);

        check(ok, "y[0]", tc.label, y[0], out_N[0][0].to_int());
        check(ok, "y[1]", tc.label, y[1], out_N[1][0].to_int());
        check(ok, "y[2]", tc.label, y[2], out_N[2][0].to_int());
        check(ok, "y[3]", tc.label, y[3], out_N[3][0].to_int());
    }

    if (ok) {
        std::printf("\nPASS: FIR_Spatial_Top_C (4 celdas, 1 invocacion, "
                    "las 4 y[n] en paralelo) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
