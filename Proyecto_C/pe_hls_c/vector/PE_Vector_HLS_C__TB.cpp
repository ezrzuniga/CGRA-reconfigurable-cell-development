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

    if (ok) printf("\nPASS: PE_Vector_HLS_C resuelve la suma lane-a-lane en los %d lanes.\n", VLEN);
    return ok ? 0 : 1;
}
