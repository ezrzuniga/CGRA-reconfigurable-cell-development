// SumReduction_Temporal_Top_C__TB.cpp
// Testbench plano (sin sc_main) de SumReduction_Temporal_Top_C: programa la
// unica celda una vez (1 llamada prog_valid), despues dispara start=true dos
// veces con vectores distintos, sin volver a programar -- mismo patron de
// "programar una vez, correr muchas" que GEMM_2x2_HLS_Top_C__TB.cpp.
//
// Primer caso (V, suma=22) es el mismo vector de 8 elementos que
// ../../Proyecto_SystemC/pe/mac/PE_MAC_SumReduction__TB.cpp, para validar
// contra un resultado ya conocido de otra implementacion del mismo problema.
#include <cstdint>
#include <cstdio>
#include <string>
#include "SumReduction_Temporal_Top_C.h"

struct SumRedCase {
    const char* label;
    int32_t v[SUMRED_T_NUM_PHASES];
    int32_t expected_total;
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

    // -- 1) Programar la unica celda una unica vez -------------------------
    SumRedTemporalInstr_C prog[SUMRED_T_ROWS][SUMRED_T_COLS][SUMRED_T_INSTR_MEM_SIZE];
    sumred_temporal_program_c(prog);

    SumRedTemporalLink_C dummy_in_N[SUMRED_T_NUM_PHASES][SUMRED_T_COLS];
    SumRedTemporalLink_C dummy_in_S[SUMRED_T_NUM_PHASES][SUMRED_T_COLS];
    SumRedTemporalLink_C dummy_in_W[SUMRED_T_NUM_PHASES][SUMRED_T_ROWS];
    SumRedTemporalLink_C dummy_in_E[SUMRED_T_NUM_PHASES][SUMRED_T_ROWS];
    SumRedTemporalLink_C dummy_out_N[SUMRED_T_COLS], dummy_out_S[SUMRED_T_COLS];
    SumRedTemporalLink_C dummy_out_W[SUMRED_T_ROWS], dummy_out_E[SUMRED_T_ROWS];

    int programmed = 0;
    for (int slot = 0; slot < SUMRED_T_INSTR_MEM_SIZE; slot++) {
        bool done = false;
        SumReduction_Temporal_Top_C(
            /*prog_valid=*/true, 0, 0, slot, prog[0][0][slot],
            /*start=*/false, done,
            dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
            dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
        if (done) programmed++;
    }
    check(ok, "programa temporal cargado", "prog_valid x" + std::to_string(SUMRED_T_INSTR_MEM_SIZE),
          SUMRED_T_INSTR_MEM_SIZE, programmed);

    // -- 2) Correr dos casos SIN volver a programar -------------------------
    SumRedCase cases[2] = {
        {"vector de referencia (PE_MAC_SumReduction__TB)",
         {6, -2, 9, 4, 0, 7, -5, 3}, 22},
        {"vector con seed grande y negativos",
         {100, -55, 30, 7, -12, 4, -9, 15}, 80},
    };

    for (const SumRedCase& tc : cases) {
        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);

        // Un elemento por FASE (temporal): in_N[fase][0] = v[fase].
        SumRedTemporalLink_C in_N[SUMRED_T_NUM_PHASES][SUMRED_T_COLS];
        SumRedTemporalLink_C in_S[SUMRED_T_NUM_PHASES][SUMRED_T_COLS];
        SumRedTemporalLink_C in_W[SUMRED_T_NUM_PHASES][SUMRED_T_ROWS];
        SumRedTemporalLink_C in_E[SUMRED_T_NUM_PHASES][SUMRED_T_ROWS];
        for (int phase = 0; phase < SUMRED_T_NUM_PHASES; phase++) {
            in_N[phase][0][0] = tc.v[phase];
        }

        SumRedTemporalLink_C out_N[SUMRED_T_COLS], out_S[SUMRED_T_COLS];
        SumRedTemporalLink_C out_W[SUMRED_T_ROWS], out_E[SUMRED_T_ROWS];
        bool done = false;
        SumRedTemporalInstr_C unused_instr;
        SumReduction_Temporal_Top_C(
            /*prog_valid=*/false, 0, 0, 0, unused_instr,
            /*start=*/true, done,
            in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
        check(ok, "total (temporal, 1 celda x 8 fases)", tc.label,
              tc.expected_total, out_E[0][0].to_int());
    }

    if (ok) {
        std::printf("\nPASS: SumReduction_Temporal_Top_C (1 celda, 8 fases, "
                    "acumulador reutilizado en el tiempo) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
