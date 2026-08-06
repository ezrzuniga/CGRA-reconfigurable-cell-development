// SumReduction16_Temporal_Top_C__TB.cpp
// Programa la unica celda una vez (3 llamadas prog_valid), despues corre 2
// casos de prueba de 16 elementos, sin volver a programar. Mismos 2
// vectores que SumReduction16_Spatial_Top_C__TB.cpp para comparar.
#include <cstdint>
#include <cstdio>
#include <string>
#include "SumReduction16_Temporal_Top_C.h"

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

    SumRed16TemporalInstr_C prog[SUMRED16_T_ROWS][SUMRED16_T_COLS][SUMRED16_T_INSTR_MEM_SIZE];
    sumred16_temporal_program_c(prog);

    SumRed16TemporalLink_C dummy_in_N[SUMRED16_T_NUM_PHASES][SUMRED16_T_COLS];
    SumRed16TemporalLink_C dummy_in_S[SUMRED16_T_NUM_PHASES][SUMRED16_T_COLS];
    SumRed16TemporalLink_C dummy_in_W[SUMRED16_T_NUM_PHASES][SUMRED16_T_ROWS];
    SumRed16TemporalLink_C dummy_in_E[SUMRED16_T_NUM_PHASES][SUMRED16_T_ROWS];
    SumRed16TemporalLink_C dummy_out_N[SUMRED16_T_COLS], dummy_out_S[SUMRED16_T_COLS];
    SumRed16TemporalLink_C dummy_out_W[SUMRED16_T_ROWS], dummy_out_E[SUMRED16_T_ROWS];

    int programmed = 0;
    for (int slot = 0; slot < SUMRED16_T_INSTR_MEM_SIZE; slot++) {
        bool done = false;
        SumReduction16_Temporal_Top_C(true, 0, 0, slot, prog[0][0][slot], false, done,
                                       dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                                       dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
        if (done) programmed++;
    }
    check(ok, "programa temporal cargado", "prog_valid x" + std::to_string(SUMRED16_T_INSTR_MEM_SIZE),
          SUMRED16_T_INSTR_MEM_SIZE, programmed);

    struct Case { const char* label; int32_t v[16]; int32_t expected_total; };
    Case cases[2] = {
        {"v16a", {6, -2, 9, 4, 0, 7, -5, 3, 2, -8, 6, 1, -3, 5, 4, -1}, 28},
        {"v16b", {10, 20, -5, 3, 7, -2, 8, 1, 4, -6, 9, 0, 2, -1, 5, 3}, 58},
    };

    for (const Case& tc : cases) {
        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);

        SumRed16TemporalLink_C in_N[SUMRED16_T_NUM_PHASES][SUMRED16_T_COLS];
        SumRed16TemporalLink_C in_S[SUMRED16_T_NUM_PHASES][SUMRED16_T_COLS];
        SumRed16TemporalLink_C in_W[SUMRED16_T_NUM_PHASES][SUMRED16_T_ROWS];
        SumRed16TemporalLink_C in_E[SUMRED16_T_NUM_PHASES][SUMRED16_T_ROWS];
        for (int phase = 0; phase < SUMRED16_T_NUM_PHASES; phase++) in_N[phase][0][0] = tc.v[phase];

        SumRed16TemporalLink_C out_N[SUMRED16_T_COLS], out_S[SUMRED16_T_COLS];
        SumRed16TemporalLink_C out_W[SUMRED16_T_ROWS], out_E[SUMRED16_T_ROWS];
        bool done = false;
        SumRed16TemporalInstr_C unused_instr;
        SumReduction16_Temporal_Top_C(false, 0, 0, 0, unused_instr, true, done,
                                       in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
        check(ok, "total (temporal, 1 celda x 16 fases)", tc.label, tc.expected_total, out_E[0][0].to_int());
    }

    if (ok) {
        std::printf("\nPASS: SumReduction16_Temporal_Top_C (1 celda, 16 fases) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
