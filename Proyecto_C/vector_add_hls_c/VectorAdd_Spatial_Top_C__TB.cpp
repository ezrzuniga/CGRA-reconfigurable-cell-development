// VectorAdd_Spatial_Top_C__TB.cpp
// Programa la unica celda una vez, despues UNA sola invocacion start=true
// calcula las 4 sumas en paralelo (SIMD, VLEN=4). Mismos 2 casos que
// VectorAdd_Temporal_Top_C__TB.cpp.
#include <cstdint>
#include <cstdio>
#include <string>
#include "VectorAdd_Spatial_Top_C.h"

struct VAddCase {
    const char* label;
    int32_t a[4];
    int32_t b[4];
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

    VectorAddSpatialInstr_C prog[VADD_S_ROWS][VADD_S_COLS][VADD_S_INSTR_MEM_SIZE];
    vector_add_spatial_program_c(prog);

    VectorAddSpatialLink_C dummy_in_N[VADD_S_NUM_PHASES][VADD_S_COLS];
    VectorAddSpatialLink_C dummy_in_S[VADD_S_NUM_PHASES][VADD_S_COLS];
    VectorAddSpatialLink_C dummy_in_W[VADD_S_NUM_PHASES][VADD_S_ROWS];
    VectorAddSpatialLink_C dummy_in_E[VADD_S_NUM_PHASES][VADD_S_ROWS];
    VectorAddSpatialLink_C dummy_out_N[VADD_S_COLS], dummy_out_S[VADD_S_COLS];
    VectorAddSpatialLink_C dummy_out_W[VADD_S_ROWS], dummy_out_E[VADD_S_ROWS];

    int programmed = 0;
    for (int slot = 0; slot < VADD_S_INSTR_MEM_SIZE; slot++) {
        bool done = false;
        VectorAdd_Spatial_Top_C(true, 0, 0, slot, prog[0][0][slot], false, done,
                                 dummy_in_N, dummy_in_S, dummy_in_W, dummy_in_E,
                                 dummy_out_N, dummy_out_S, dummy_out_W, dummy_out_E);
        if (done) programmed++;
    }
    check(ok, "programa espacial cargado", "prog_valid x" + std::to_string(VADD_S_INSTR_MEM_SIZE),
          VADD_S_INSTR_MEM_SIZE, programmed);

    VAddCase cases[2] = {
        {"positivos", {1, 2, 3, 4}, {10, 20, 30, 40}},
        {"con negativos", {-5, 7, -3, 9}, {2, -8, 6, -1}},
    };

    for (const VAddCase& tc : cases) {
        int32_t c[4];
        for (int n = 0; n < 4; n++) c[n] = tc.a[n] + tc.b[n];

        std::printf("\n==== Caso: %s (sin reprogramar) ====\n", tc.label);
        std::printf("a=[%d,%d,%d,%d] b=[%d,%d,%d,%d] c esperado=[%d,%d,%d,%d]\n",
                    tc.a[0], tc.a[1], tc.a[2], tc.a[3], tc.b[0], tc.b[1], tc.b[2], tc.b[3],
                    c[0], c[1], c[2], c[3]);

        VectorAddSpatialLink_C in_N[VADD_S_NUM_PHASES][VADD_S_COLS];
        VectorAddSpatialLink_C in_S[VADD_S_NUM_PHASES][VADD_S_COLS];
        VectorAddSpatialLink_C in_W[VADD_S_NUM_PHASES][VADD_S_ROWS];
        VectorAddSpatialLink_C in_E[VADD_S_NUM_PHASES][VADD_S_ROWS];
        for (int n = 0; n < 4; n++) {
            in_N[0][0][n] = tc.a[n];
            in_S[0][0][n] = tc.b[n];
        }

        VectorAddSpatialLink_C out_N[VADD_S_COLS], out_S[VADD_S_COLS];
        VectorAddSpatialLink_C out_W[VADD_S_ROWS], out_E[VADD_S_ROWS];
        bool done = false;
        VectorAddSpatialInstr_C unused_instr;
        VectorAdd_Spatial_Top_C(false, 0, 0, 0, unused_instr, true, done,
                                 in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
        check(ok, "done se activo", tc.label, 1, done ? 1 : 0);

        for (int n = 0; n < 4; n++) {
            check(ok, "c[" + std::to_string(n) + "]", tc.label, c[n], out_E[0][n].to_int());
        }
    }

    if (ok) {
        std::printf("\nPASS: VectorAdd_Spatial_Top_C (1 celda SIMD VLEN=4, 1 invocacion, "
                    "las 4 sumas en paralelo) resuelve ambos casos.\n");
    }
    return ok ? 0 : 1;
}
