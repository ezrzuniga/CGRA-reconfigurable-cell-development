// PE_Scalar_HLS_C__TB.cpp
// Testbench plano (sin sc_main) de PE_Scalar_HLS_C.h: programa 4 slots via
// pe_scalar_program(), corre 4 ciclos de pe_scalar_step(), y verifica que el
// resultado escalar se difunde correctamente a los VLEN lanes de out_E
// (prueba el puente lane0-en-lectura / broadcast-en-escritura plegado en
// pe_scalar_step()).
#include <cstdio>
#include "PE_Scalar_HLS_C.h"

static const int DATA_W = 32, VLEN = 4, NUM_REGS = 8, INSTR_MEM_SIZE = 4;
typedef PE_Scalar_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE> State;
typedef PE_Instruction<DATA_W> Instr;

static Instr mov_imm(ap_int<DATA_W> imm, ap_uint<3> dst, ap_uint<5> reg_dst = 0) {
    Instr i;
    i.opcode = OP_MOV;
    i.src_a = SRC_IMM;
    i.imm = imm;
    i.dst = dst;
    i.reg_dst = reg_dst;
    return i;
}

static Instr add_reg_west(ap_uint<5> reg_a, ap_uint<5> reg_dst) {
    Instr i;
    i.opcode = OP_ADD;
    i.src_a = SRC_REG;
    i.reg_a = reg_a;
    i.src_b = SRC_WEST;
    i.dst = DST_REG;
    i.reg_dst = reg_dst;
    return i;
}

static Instr mov_reg_to_east(ap_uint<5> reg_a) {
    Instr i;
    i.opcode = OP_MOV;
    i.src_a = SRC_REG;
    i.reg_a = reg_a;
    i.dst = DST_EAST;
    return i;
}

int main() {
    bool ok = true;

    State s;
    pe_scalar_program(s, 0, mov_imm(5, DST_REG, 0));   // slot0: reg0 = 5
    pe_scalar_program(s, 1, add_reg_west(0, 1));       // slot1: reg1 = reg0 + in_W
    pe_scalar_program(s, 2, mov_reg_to_east(1));        // slot2: out_E = reg1 (broadcast)
    pe_scalar_program(s, 3, Instr());                   // slot3: NOP

    PE_VectorData<DATA_W, VLEN> in_N, in_S, in_E, in_W;
    in_W[0] = 10;

    pe_scalar_step(s, /*rst=*/true, /*enable=*/true, in_N, in_S, in_E, in_W);  // pc -> 0
    for (int i = 0; i < 4; i++) pe_scalar_step(s, false, true, in_N, in_S, in_E, in_W);

    int32_t expected = 5 + 10;
    bool pass_reg = (s.reg_file[1].to_int() == expected);
    printf("%s reg_file[1] = reg0 + in_W  esperado=%d obtenido=%d\n",
           pass_reg ? "PASS" : "FAIL", expected, s.reg_file[1].to_int());
    if (!pass_reg) ok = false;

    for (int lane = 0; lane < VLEN; lane++) {
        bool pass = (s.out_E[lane].to_int() == expected);
        printf("%s out_E[%d] (broadcast) esperado=%d obtenido=%d\n",
               pass ? "PASS" : "FAIL", lane, expected, s.out_E[lane].to_int());
        if (!pass) ok = false;
    }

    if (ok) printf("\nPASS: PE_Scalar_HLS_C resuelve el programa de prueba y difunde a los %d lanes.\n", VLEN);
    return ok ? 0 : 1;
}
