// MaxReduction_Temporal_Top_C__TB.cpp
// Programa la unica celda una vez (7 llamadas prog_valid), despues corre 2
// casos de prueba (mismos vectores de referencia que sum_reduction_hls_c,
// cuyo maximo real es >=0, ver comentario de cabecera de
// MaxReduction_Temporal_Mesh_C.h) sin volver a programar.
#include <cstdint>
#include <cstdio>
#include <string>
#include "MaxReduction_Temporal_Top_C.h"

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

    MaxRedTemporalInstr_C prog[MAXRED_T_ROWS][MAXRED_T_COLS][MAXRED_T_INSTR_MEM_SIZE];
    maxred_temporal_program_c(prog);

    MaxRedTemporalLink_C dummy_in_N[MAXRED_T_NUM_PHASES][MAXRED_T_COLS];
    MaxRedTemporalLink_C dummy_in_S[MAXRED_T_NUM_PHASES][MAXRED_T_COLS];
    MaxRedTemporalLink_C dummy_in_W[MAXRED_T_NUM_PHASES][MAXRED_T_ROWS];
    MaxRedTemporalLink_C dummy_in_E[MAXRED_T_NUM_PHASES][MAXRED_T_ROWS];
    MaxRedTemporalLink_C dummy_out_N[MAXRED_T_COLS], dummy_out_S[MAXRED_T_COLS];
    MaxRedTemporalLink_C dummy_out_W[MAXRED_T_ROWS], dummy_out_E[MAXRED_T_ROWS];

    int programmed = 0;
    for (int slot = 0; slot < MAXRED_T_INSTR_MEM_SIZE; slot++) {
        bool done = false;
        MaxReduction_Temporal_Top_C(true, 0, 0, slot, prog[0][0][slot], false, done,
                                     dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                                     dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
        if (done) programmed++;
    }
    check(ok, "programa temporal cargado", "prog_valid x" + std::to_string(MAXRED_T_INSTR_MEM_SIZE),
          MAXRED_T_INSTR_MEM_SIZE, programmed);

    struct Case { const char* label; int32_t v[8]; int32_t expected_max; };
    Case cases[2] = {
        {"vector de referencia", {6, -2, 9, 4, 0, 7, -5, 3}, 9},
        {"vector con seed grande", {100, -55, 30, 7, -12, 4, -9, 15}, 100},
    };

    for (const Case& tc : cases) {
        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);

        MaxRedTemporalLink_C in_N[MAXRED_T_NUM_PHASES][MAXRED_T_COLS];
        MaxRedTemporalLink_C in_S[MAXRED_T_NUM_PHASES][MAXRED_T_COLS];
        MaxRedTemporalLink_C in_W[MAXRED_T_NUM_PHASES][MAXRED_T_ROWS];
        MaxRedTemporalLink_C in_E[MAXRED_T_NUM_PHASES][MAXRED_T_ROWS];
        for (int phase = 0; phase < MAXRED_T_NUM_PHASES; phase++) in_N[phase][0][0] = tc.v[phase];

        MaxRedTemporalLink_C out_N[MAXRED_T_COLS], out_S[MAXRED_T_COLS];
        MaxRedTemporalLink_C out_W[MAXRED_T_ROWS], out_E[MAXRED_T_ROWS];
        bool done = false;
        MaxRedTemporalInstr_C unused_instr;
        MaxReduction_Temporal_Top_C(false, 0, 0, 0, unused_instr, true, done,
                                     in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
        check(ok, "maximo (temporal, 1 celda x 8 fases)", tc.label, tc.expected_max, out_E[0][0].to_int());
    }

    if (ok) {
        std::printf("\nPASS: MaxReduction_Temporal_Top_C (1 celda, 8 fases, "
                    "maximo via SUB/SLT/MUL/ADD) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
