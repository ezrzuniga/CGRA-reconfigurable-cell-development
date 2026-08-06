// VectorAdd_Temporal_Top_C__TB.cpp
// Programa la unica celda una vez (1 llamada prog_valid), despues calcula
// cada c[n] con una invocacion start=true separada (4 por caso) -- sin
// SIMD, un elemento escalar por invocacion. Mismos 2 casos que
// VectorAdd_Spatial_Top_C__TB.cpp.
#include <cstdint>
#include <cstdio>
#include <string>
#include "VectorAdd_Temporal_Top_C.h"

struct VAddCase {
    const char* label;
    int32_t a[VADD_N];
    int32_t b[VADD_N];
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

    VectorAddTemporalInstr_C prog[VADD_T_ROWS][VADD_T_COLS][VADD_T_INSTR_MEM_SIZE];
    vector_add_temporal_program_c(prog);

    VectorAddTemporalLink_C dummy_in_N[VADD_T_NUM_PHASES][VADD_T_COLS];
    VectorAddTemporalLink_C dummy_in_S[VADD_T_NUM_PHASES][VADD_T_COLS];
    VectorAddTemporalLink_C dummy_in_W[VADD_T_NUM_PHASES][VADD_T_ROWS];
    VectorAddTemporalLink_C dummy_in_E[VADD_T_NUM_PHASES][VADD_T_ROWS];
    VectorAddTemporalLink_C dummy_out_N[VADD_T_COLS], dummy_out_S[VADD_T_COLS];
    VectorAddTemporalLink_C dummy_out_W[VADD_T_ROWS], dummy_out_E[VADD_T_ROWS];

    int programmed = 0;
    for (int slot = 0; slot < VADD_T_INSTR_MEM_SIZE; slot++) {
        bool done = false;
        VectorAdd_Temporal_Top_C(true, 0, 0, slot, prog[0][0][slot], false, done,
                                  dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                                  dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
        if (done) programmed++;
    }
    check(ok, "programa temporal cargado", "prog_valid x" + std::to_string(VADD_T_INSTR_MEM_SIZE),
          VADD_T_INSTR_MEM_SIZE, programmed);

    VAddCase cases[2] = {
        {"positivos", {1, 2, 3, 4}, {10, 20, 30, 40}},
        {"con negativos", {-5, 7, -3, 9}, {2, -8, 6, -1}},
    };

    for (const VAddCase& tc : cases) {
        int32_t c[VADD_N];
        for (int n = 0; n < VADD_N; n++) c[n] = tc.a[n] + tc.b[n];

        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);
        std::printf("a=[%d,%d,%d,%d] b=[%d,%d,%d,%d] c esperado=[%d,%d,%d,%d]\n",
                    tc.a[0], tc.a[1], tc.a[2], tc.a[3], tc.b[0], tc.b[1], tc.b[2], tc.b[3],
                    c[0], c[1], c[2], c[3]);

        int32_t got[VADD_N];
        for (int n = 0; n < VADD_N; n++) {
            VectorAddTemporalLink_C in_N[VADD_T_NUM_PHASES][VADD_T_COLS];
            VectorAddTemporalLink_C in_S[VADD_T_NUM_PHASES][VADD_T_COLS];
            VectorAddTemporalLink_C in_W[VADD_T_NUM_PHASES][VADD_T_ROWS];
            VectorAddTemporalLink_C in_E[VADD_T_NUM_PHASES][VADD_T_ROWS];
            in_N[0][0][0] = tc.a[n];
            in_S[0][0][0] = tc.b[n];

            VectorAddTemporalLink_C out_N[VADD_T_COLS], out_S[VADD_T_COLS];
            VectorAddTemporalLink_C out_W[VADD_T_ROWS], out_E[VADD_T_ROWS];
            bool done = false;
            VectorAddTemporalInstr_C unused_instr;
            VectorAdd_Temporal_Top_C(false, 0, 0, 0, unused_instr, true, done,
                                      in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
            check(ok, "done se activo", tc.label, 1, done ? 1 : 0);
            got[n] = out_E[0][0].to_int();
        }

        for (int n = 0; n < VADD_N; n++) {
            check(ok, "c[" + std::to_string(n) + "]", tc.label, c[n], got[n]);
        }
    }

    if (ok) {
        std::printf("\nPASS: VectorAdd_Temporal_Top_C (1 celda escalar, 4 invocaciones, "
                    "un elemento por vez) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
