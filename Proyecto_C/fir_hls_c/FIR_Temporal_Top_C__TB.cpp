// FIR_Temporal_Top_C__TB.cpp
// Programa la unica celda una vez (3 llamadas prog_valid), despues calcula
// cada y[n] con una invocacion start=true separada (4 por caso), sin volver
// a programar -- mismo patron que GEMM_2x2_Temporal_Top_C__TB.cpp. Mismos 2
// casos de prueba que FIR_Spatial_Top_C__TB.cpp para comparar directamente.
#include <cstdint>
#include <cstdio>
#include <string>
#include "FIR_Temporal_Top_C.h"

struct FirCase {
    const char* label;
    int32_t w[FIR_TAPS];
    int32_t x[FIR_TAPS + 3]; // 6 muestras -> 4 salidas con un filtro de 3 taps
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

    FirTemporalInstr_C prog[FIR_T_ROWS][FIR_T_COLS][FIR_T_INSTR_MEM_SIZE];
    fir_temporal_program_c(prog);

    FirTemporalLink_C dummy_in_N[FIR_T_NUM_PHASES][FIR_T_COLS];
    FirTemporalLink_C dummy_in_S[FIR_T_NUM_PHASES][FIR_T_COLS];
    FirTemporalLink_C dummy_in_W[FIR_T_NUM_PHASES][FIR_T_ROWS];
    FirTemporalLink_C dummy_in_E[FIR_T_NUM_PHASES][FIR_T_ROWS];
    FirTemporalLink_C dummy_out_N[FIR_T_COLS], dummy_out_S[FIR_T_COLS];
    FirTemporalLink_C dummy_out_W[FIR_T_ROWS], dummy_out_E[FIR_T_ROWS];

    int programmed = 0;
    for (int slot = 0; slot < FIR_T_INSTR_MEM_SIZE; slot++) {
        bool done = false;
        FIR_Temporal_Top_C(true, 0, 0, slot, prog[0][0][slot], false, done,
                            dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                            dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
        if (done) programmed++;
    }
    check(ok, "programa temporal cargado", "prog_valid x" + std::to_string(FIR_T_INSTR_MEM_SIZE),
          FIR_T_INSTR_MEM_SIZE, programmed);

    FirCase cases[2] = {
        {"pesos/muestras positivos", {1, 2, 3}, {1, 2, 3, 4, 5, 6}},
        {"con valores negativos", {2, -1, 3}, {5, -2, 4, 1, -3, 6}},
    };

    for (const FirCase& tc : cases) {
        int32_t y[4];
        for (int n = 0; n < 4; n++) {
            int32_t acc = 0;
            for (int k = 0; k < FIR_TAPS; k++) acc += tc.w[k] * tc.x[n + k];
            y[n] = acc;
        }

        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);
        std::printf("w=[%d,%d,%d] x=[%d,%d,%d,%d,%d,%d] y esperado=[%d,%d,%d,%d]\n",
                    tc.w[0], tc.w[1], tc.w[2], tc.x[0], tc.x[1], tc.x[2], tc.x[3], tc.x[4], tc.x[5],
                    y[0], y[1], y[2], y[3]);

        int32_t got[4];
        for (int n = 0; n < 4; n++) {
            FirTemporalLink_C in_N[FIR_T_NUM_PHASES][FIR_T_COLS];
            FirTemporalLink_C in_S[FIR_T_NUM_PHASES][FIR_T_COLS];
            FirTemporalLink_C in_W[FIR_T_NUM_PHASES][FIR_T_ROWS];
            FirTemporalLink_C in_E[FIR_T_NUM_PHASES][FIR_T_ROWS];
            for (int k = 0; k < FIR_TAPS; k++) {
                in_N[k][0][0] = tc.w[k];
                in_S[k][0][0] = tc.x[n + k];
            }

            FirTemporalLink_C out_N[FIR_T_COLS], out_S[FIR_T_COLS];
            FirTemporalLink_C out_W[FIR_T_ROWS], out_E[FIR_T_ROWS];
            bool done = false;
            FirTemporalInstr_C unused_instr;
            FIR_Temporal_Top_C(false, 0, 0, 0, unused_instr, true, done,
                                in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
            check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
            got[n] = out_E[0][0].to_int();
        }

        check(ok, "y[0]", tc.label, y[0], got[0]);
        check(ok, "y[1]", tc.label, y[1], got[1]);
        check(ok, "y[2]", tc.label, y[2], got[2]);
        check(ok, "y[3]", tc.label, y[3], got[3]);
    }

    if (ok) {
        std::printf("\nPASS: FIR_Temporal_Top_C (1 celda, 4 invocaciones start=true, "
                    "una por y[n]) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
