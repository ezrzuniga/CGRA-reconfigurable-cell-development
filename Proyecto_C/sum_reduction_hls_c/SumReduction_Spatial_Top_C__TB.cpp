// SumReduction_Spatial_Top_C__TB.cpp
// Testbench plano (sin sc_main) de SumReduction_Spatial_Top_C: programa las
// 4 celdas una unica vez (12 llamadas prog_valid: 4 celdas x 3 slots),
// despues dispara start=true dos veces con vectores distintos, sin volver a
// programar -- mismo patron que SumReduction_Temporal_Top_C__TB.cpp y
// GEMM_2x2_HLS_Top_C__TB.cpp. Mismos 2 vectores/casos que el testbench
// temporal para que los resultados (y, corridos por Vitis HLS, los ciclos)
// sean directamente comparables entre los dos mapeos.
#include <cstdint>
#include <cstdio>
#include <string>
#include "SumReduction_Spatial_Top_C.h"

struct SumRedCase {
    const char* label;
    int32_t v[8]; // v0..v7, ver encabezado de SumReduction_Spatial_Mesh_C.h para la asignacion a puertos
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

    // -- 1) Programar las 4 celdas una unica vez ----------------------------
    SumRedSpatialInstr_C prog[SUMRED_S_ROWS][SUMRED_S_COLS][SUMRED_S_INSTR_MEM_SIZE];
    sumred_spatial_program_c(prog);

    SumRedSpatialLink_C dummy_in_N[SUMRED_S_NUM_PHASES][SUMRED_S_COLS];
    SumRedSpatialLink_C dummy_in_S[SUMRED_S_NUM_PHASES][SUMRED_S_COLS];
    SumRedSpatialLink_C dummy_in_W[SUMRED_S_NUM_PHASES][SUMRED_S_ROWS];
    SumRedSpatialLink_C dummy_in_E[SUMRED_S_NUM_PHASES][SUMRED_S_ROWS];
    SumRedSpatialLink_C dummy_out_N[SUMRED_S_COLS], dummy_out_S[SUMRED_S_COLS];
    SumRedSpatialLink_C dummy_out_W[SUMRED_S_ROWS], dummy_out_E[SUMRED_S_ROWS];

    int programmed = 0;
    const int total_instrs = SUMRED_S_ROWS * SUMRED_S_COLS * SUMRED_S_INSTR_MEM_SIZE;
    for (int r = 0; r < SUMRED_S_ROWS; r++) {
        for (int c = 0; c < SUMRED_S_COLS; c++) {
            for (int slot = 0; slot < SUMRED_S_INSTR_MEM_SIZE; slot++) {
                bool done = false;
                SumReduction_Spatial_Top_C(
                    /*prog_valid=*/true, r, c, slot, prog[r][c][slot],
                    /*start=*/false, done,
                    dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                    dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
                if (done) programmed++;
            }
        }
    }
    check(ok, "programa espacial (arbol) cargado", "prog_valid x" + std::to_string(total_instrs),
          total_instrs, programmed);

    // -- 2) Correr dos casos SIN volver a programar -------------------------
    SumRedCase cases[2] = {
        {"vector de referencia (mismo que el testbench temporal)",
         {6, -2, 9, 4, 0, 7, -5, 3}, 22},
        {"vector con seed grande y negativos (mismo que el testbench temporal)",
         {100, -55, 30, 7, -12, 4, -9, 15}, 80},
    };

    for (const SumRedCase& tc : cases) {
        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);

        // Los 8 elementos entran TODOS en la unica fase, uno por puerto
        // externo (ver encabezado de SumReduction_Spatial_Mesh_C.h):
        //   P00.N=v0 P00.W=v1   P01.N=v2 P01.E=v3
        //   P10.S=v4 P10.W=v5   P11.S=v6 P11.E=v7
        SumRedSpatialLink_C in_N[SUMRED_S_NUM_PHASES][SUMRED_S_COLS];
        SumRedSpatialLink_C in_S[SUMRED_S_NUM_PHASES][SUMRED_S_COLS];
        SumRedSpatialLink_C in_W[SUMRED_S_NUM_PHASES][SUMRED_S_ROWS];
        SumRedSpatialLink_C in_E[SUMRED_S_NUM_PHASES][SUMRED_S_ROWS];
        in_N[0][0][0] = tc.v[0]; in_W[0][0][0] = tc.v[1];
        in_N[0][1][0] = tc.v[2]; in_E[0][0][0] = tc.v[3];
        in_S[0][0][0] = tc.v[4]; in_W[0][1][0] = tc.v[5];
        in_S[0][1][0] = tc.v[6]; in_E[0][1][0] = tc.v[7];

        SumRedSpatialLink_C out_N[SUMRED_S_COLS], out_S[SUMRED_S_COLS];
        SumRedSpatialLink_C out_W[SUMRED_S_ROWS], out_E[SUMRED_S_ROWS];
        bool done = false;
        SumRedSpatialInstr_C unused_instr;
        SumReduction_Spatial_Top_C(
            /*prog_valid=*/false, 0, 0, 0, unused_instr,
            /*start=*/true, done,
            in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
        // Total sale por el borde este externo de P11 (fila 1) -- out_E[1].
        check(ok, "total (espacial, 4 celdas x 3 niveles de arbol)", tc.label,
              tc.expected_total, out_E[1][0].to_int());
    }

    if (ok) {
        std::printf("\nPASS: SumReduction_Spatial_Top_C (4 celdas, 1 fase, "
                    "arbol de sumas en paralelo) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
