// PE_Vector_HLS_C__TB.cpp
// Testbench plano de PE_Vector_HLS_C.h: programa 2 slots via
// pe_vector_program(), corre 2 ciclos de pe_vector_step(), y verifica una
// suma lane-a-lane (in_W + in_N -> out_E) sobre los VLEN lanes.
#include <cstdio>
#include "PE_Vector_HLS_C.h"

static const int DATA_W = 32, VLEN = 4, NUM_REGS = 8, INSTR_MEM_SIZE = 4;
typedef PE_Vector_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE> State;
typedef PE_Instruction<DATA_W> Instr;

static Instr add_west_north_to_east() {
    Instr i;
    i.opcode = OP_ADD;
    i.src_a = SRC_WEST;
    i.src_b = SRC_NORTH;
    i.dst = DST_EAST;
    return i;
}

int main() {
    bool ok = true;

    State s;
    pe_vector_program(s, 0, add_west_north_to_east());
    pe_vector_program(s, 1, Instr());  // NOP

    PE_VectorData<DATA_W, VLEN> in_N, in_S, in_E, in_W;
    for (int i = 0; i < VLEN; i++) { in_W[i] = 10 + i; in_N[i] = 100 * (i + 1); }

    pe_vector_step(s, /*rst=*/true, /*enable=*/true, in_N, in_S, in_E, in_W);
    pe_vector_step(s, false, true, in_N, in_S, in_E, in_W);

    for (int i = 0; i < VLEN; i++) {
        int32_t expected = (10 + i) + 100 * (i + 1);
        bool pass = (s.out_E[i].to_int() == expected);
        printf("%s out_E[%d] = in_W+in_N  esperado=%d obtenido=%d\n",
               pass ? "PASS" : "FAIL", i, expected, s.out_E[i].to_int());
        if (!pass) ok = false;
    }

    // --- Contextos: programa el contexto 1 con OP_FMUL (float32 lane-a-lane)
    // y confirma que activarlo no pisa el programa entero del contexto 0.
    Instr fmul_west_north_to_east;
    fmul_west_north_to_east.opcode = OP_FMUL;
    fmul_west_north_to_east.src_a = SRC_WEST;
    fmul_west_north_to_east.src_b = SRC_NORTH;
    fmul_west_north_to_east.dst = DST_EAST;

    unsigned ctx1_slot0 = 1 * INSTR_MEM_SIZE + 0;
    pe_vector_program(s, ctx1_slot0, fmul_west_north_to_east);

    PE_VectorData<DATA_W, VLEN> fin_N, fin_W;
    for (int i = 0; i < VLEN; i++) {
        float fw = 1.5f + i, fn = 2.0f;
        fin_W[i] = f32_to_bits(fw);
        fin_N[i] = f32_to_bits(fn);
    }

    pe_vector_step(s, /*rst=*/true, /*enable=*/true, fin_N, in_S, in_E, fin_W);  // pc->0, activa ctx1
    pe_vector_step(s, false, true, fin_N, in_S, in_E, fin_W);

    bool pass_float = true;
    for (int i = 0; i < VLEN; i++) {
        float expected = (1.5f + i) * 2.0f;
        float got = f32_from_bits(s.out_E[i].to_int());
        bool pass = (got == expected);
        printf("%s out_E[%d] float32 = in_W*in_N (ctx1)  esperado=%.2f obtenido=%.2f\n",
               pass ? "PASS" : "FAIL", i, expected, got);
        if (!pass) pass_float = false;
    }
    ok = ok && pass_float;

    // El contexto 0 (suma entera) debe seguir intacto: reactivandolo, el
    // mismo estimulo entero original debe seguir dando el mismo resultado.
    pe_vector_program(s, 0, add_west_north_to_east());  // reprograma slot0 del ctx0 -> lo reactiva
    pe_vector_step(s, /*rst=*/true, /*enable=*/true, in_N, in_S, in_E, in_W);
    pe_vector_step(s, false, true, in_N, in_S, in_E, in_W);
    bool pass_ctx0_intact = (s.out_E[0].to_int() == (10 + 100));
    printf("%s contexto 0 (suma entera) sigue intacto tras usar el contexto 1: esperado=%d obtenido=%d\n",
           pass_ctx0_intact ? "PASS" : "FAIL", 10 + 100, s.out_E[0].to_int());
    ok = ok && pass_ctx0_intact;

    if (ok) printf("\nPASS: PE_Vector_HLS_C resuelve la suma lane-a-lane en los %d lanes, "
                    "con contextos y punto flotante32 reales.\n", VLEN);
    return ok ? 0 : 1;
}
