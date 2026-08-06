// SumReduction16_Spatial_Top_C__TB.cpp
// Secuencia completa de 2 etapas (ver comentario de cabecera de
// SumReduction16_Spatial_Top_C.h): programar etapa1 (28 prog_valid) ->
// correr etapa1 (start,stage2=false) -> reprogramar etapa2 (28 prog_valid,
// reg_file sobrevive) -> correr etapa2 (start,stage2=true) -> leer
// resultado. Mismos 2 vectores de 16 elementos que
// SumReduction16_Temporal_Top_C__TB.cpp.
#include <cstdint>
#include <cstdio>
#include <string>
#include "SumReduction16_Spatial_Top_C.h"

static bool check(bool& ok, const std::string& label, const std::string& inputs,
                   int32_t expected, int32_t got) {
    bool pass = (got == expected);
    std::printf("%s %s\n  entrada  : %s\n  esperado : %d\n  obtenido : %d\n",
                pass ? "PASS" : "FAIL", label.c_str(), inputs.c_str(), expected, got);
    if (!pass) ok = false;
    return pass;
}

static int program_mesh(
    SumRed16SpatialInstr_C prog[SUMRED16_S_ROWS][SUMRED16_S_COLS][SUMRED16_S_INSTR_MEM_SIZE])
{
    SumRed16SpatialLink_C dummy_in_N[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS];
    SumRed16SpatialLink_C dummy_in_S[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS];
    SumRed16SpatialLink_C dummy_in_W[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS];
    SumRed16SpatialLink_C dummy_in_E[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS];
    SumRed16SpatialLink_C dummy_out_N[SUMRED16_S_COLS], dummy_out_S[SUMRED16_S_COLS];
    SumRed16SpatialLink_C dummy_out_W[SUMRED16_S_ROWS], dummy_out_E[SUMRED16_S_ROWS];

    int programmed = 0;
    for (int r = 0; r < SUMRED16_S_ROWS; r++) {
        for (int c = 0; c < SUMRED16_S_COLS; c++) {
            for (int slot = 0; slot < SUMRED16_S_INSTR_MEM_SIZE; slot++) {
                bool done = false;
                SumReduction16_Spatial_Top_C(true, r, c, slot, prog[r][c][slot], false, 0, done,
                                              dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                                              dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
                if (done) programmed++;
            }
        }
    }
    return programmed;
}

int main() {
    bool ok = true;
    const int total_instrs = SUMRED16_S_ROWS * SUMRED16_S_COLS * SUMRED16_S_INSTR_MEM_SIZE;

    struct Case { const char* label; int32_t v[16]; int32_t expected_total; };
    Case cases[2] = {
        {"v16a", {6, -2, 9, 4, 0, 7, -5, 3, 2, -8, 6, 1, -3, 5, 4, -1}, 28},
        {"v16b", {10, 20, -5, 3, 7, -2, 8, 1, 4, -6, 9, 0, 2, -1, 5, 3}, 58},
    };

    for (const Case& tc : cases) {
        std::printf("\n==== Caso: %s ====\n", tc.label);

        // -- Etapa 0: limpiar reg0 (el mesh es `static` y persiste entre
        //    casos -- sin esto, el 2do caso acumularia sobre el reg0 que
        //    dejo el 1ro; bug real encontrado al validar con 2 casos) -----
        SumRed16SpatialInstr_C reset_prog[SUMRED16_S_ROWS][SUMRED16_S_COLS][SUMRED16_S_INSTR_MEM_SIZE];
        sumred16_stage0_reset_program_c(reset_prog);
        int p0 = program_mesh(reset_prog);
        check(ok, "etapa0 (reset) programada", tc.label, total_instrs, p0);
        {
            SumRed16SpatialLink_C zn[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS];
            SumRed16SpatialLink_C zs[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS];
            SumRed16SpatialLink_C zw[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS];
            SumRed16SpatialLink_C ze[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS];
            SumRed16SpatialLink_C zon[SUMRED16_S_COLS], zos[SUMRED16_S_COLS];
            SumRed16SpatialLink_C zow[SUMRED16_S_ROWS], zoe[SUMRED16_S_ROWS];
            bool rdone = false;
            SumRed16SpatialInstr_C unused0;
            SumReduction16_Spatial_Top_C(false, 0, 0, 0, unused0, true, 0, rdone,
                                          zn, zs, zw, ze, zon, zos, zow, zoe);
            check(ok, "etapa0 done", tc.label, 1, rdone ? 1 : 0);
        }

        // -- Etapa 1: programar + correr (acumular 4 elementos/celda) -----
        SumRed16SpatialInstr_C stage1_prog[SUMRED16_S_ROWS][SUMRED16_S_COLS][SUMRED16_S_INSTR_MEM_SIZE];
        sumred16_stage1_program_all_c(stage1_prog);
        int p1 = program_mesh(stage1_prog);
        check(ok, "etapa1 programada", tc.label, total_instrs, p1);

        SumRed16SpatialLink_C in_N[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS];
        SumRed16SpatialLink_C in_S[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_COLS];
        SumRed16SpatialLink_C in_W[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS];
        SumRed16SpatialLink_C in_E[SUMRED16_S_STAGE1_NUM_PHASES][SUMRED16_S_ROWS];
        // v[0..15] -> 4 por celda, 2 por fase (ver cabecera del mesh header).
        in_N[0][0][0] = tc.v[0];  in_N[1][0][0] = tc.v[1];   // P00.N
        in_N[0][1][0] = tc.v[4];  in_N[1][1][0] = tc.v[5];   // P01.N
        in_S[0][0][0] = tc.v[8];  in_S[1][0][0] = tc.v[9];   // P10.S
        in_S[0][1][0] = tc.v[12]; in_S[1][1][0] = tc.v[13];  // P11.S
        in_W[0][0][0] = tc.v[2];  in_W[1][0][0] = tc.v[3];   // P00.W
        in_W[0][1][0] = tc.v[10]; in_W[1][1][0] = tc.v[11];  // P10.W
        in_E[0][0][0] = tc.v[6];  in_E[1][0][0] = tc.v[7];   // P01.E
        in_E[0][1][0] = tc.v[14]; in_E[1][1][0] = tc.v[15];  // P11.E

        SumRed16SpatialLink_C out_N[SUMRED16_S_COLS], out_S[SUMRED16_S_COLS];
        SumRed16SpatialLink_C out_W[SUMRED16_S_ROWS], out_E[SUMRED16_S_ROWS];
        bool done = false;
        SumRed16SpatialInstr_C unused_instr;
        SumReduction16_Spatial_Top_C(false, 0, 0, 0, unused_instr, true, 1, done,
                                      in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
        check(ok, "etapa1 done", tc.label, 1, done ? 1 : 0);

        // -- Etapa 2: reprogramar (reg_file sobrevive) + correr (arbol) ---
        SumRed16SpatialInstr_C stage2_prog[SUMRED16_S_ROWS][SUMRED16_S_COLS][SUMRED16_S_INSTR_MEM_SIZE];
        sumred16_stage2_program_c(stage2_prog);
        int p2 = program_mesh(stage2_prog);
        check(ok, "etapa2 programada", tc.label, total_instrs, p2);

        done = false;
        SumReduction16_Spatial_Top_C(false, 0, 0, 0, unused_instr, true, 2, done,
                                      in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
        check(ok, "etapa2 done", tc.label, 1, done ? 1 : 0);

        // Resultado final sale por el borde este externo de P11 (fila 1).
        check(ok, "total (espacial, 4 celdas x 2 etapas)", tc.label,
              tc.expected_total, out_E[1][0].to_int());
    }

    if (ok) {
        std::printf("\nPASS: SumReduction16_Spatial_Top_C (4 celdas, 2 etapas: "
                    "acumular hojas + combinar arbol) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
