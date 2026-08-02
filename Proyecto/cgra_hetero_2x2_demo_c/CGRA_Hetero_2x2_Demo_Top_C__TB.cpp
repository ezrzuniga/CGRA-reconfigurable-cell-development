// CGRA_Hetero_2x2_Demo_Top_C__TB.cpp
// Testbench plano de CGRA_Hetero_2x2_Demo_Top_C: mismo escenario que
// mesh_hls_c/CGRA_Mesh_2x2_Heterogeneous_HLS_C__TB.cpp (round trip
// Routing<->Memoria + pipeline Scalar->Vector), pero orquestado a traves del
// top (prog_valid para programar, rst/enable + step para correr un ciclo)
// en vez de llamar mesh_program()/mesh_step() directamente -- prueba la
// interfaz que de verdad ve Vitis HLS.
#include <cstdio>
#include "CGRA_Hetero_2x2_Demo_Top_C.h"

static void step(bool rst, HeteroLink_C in_N[HETERO_COLS], HeteroLink_C in_S[HETERO_COLS],
                  HeteroLink_C in_W[HETERO_ROWS], HeteroLink_C in_E[HETERO_ROWS],
                  HeteroLink_C out_N[HETERO_COLS], HeteroLink_C out_S[HETERO_COLS],
                  HeteroLink_C out_W[HETERO_ROWS], HeteroLink_C out_E[HETERO_ROWS]) {
    HeteroInstr_C unused;
    CGRA_Hetero_2x2_Demo_Top_C(/*prog_valid=*/false, 0, 0, 0, unused, rst, /*enable=*/true,
                                in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}

static void program(ap_uint<8> row, ap_uint<8> col, ap_uint<8> slot, const HeteroInstr_C& instr) {
    HeteroLink_C dN[HETERO_COLS], dS[HETERO_COLS], dW[HETERO_ROWS], dE[HETERO_ROWS];
    HeteroLink_C oN[HETERO_COLS], oS[HETERO_COLS], oW[HETERO_ROWS], oE[HETERO_ROWS];
    CGRA_Hetero_2x2_Demo_Top_C(/*prog_valid=*/true, row, col, slot, instr, false, true,
                                dN, dS, dW, dE, oN, oS, oW, oE);
}

int main() {
    bool ok = true;

    HeteroLink_C in_N[HETERO_COLS], in_S[HETERO_COLS], in_W[HETERO_ROWS], in_E[HETERO_ROWS];
    HeteroLink_C out_N[HETERO_COLS], out_S[HETERO_COLS], out_W[HETERO_ROWS], out_E[HETERO_ROWS];

    step(/*rst=*/true, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

    // -- Routing (0,0): oeste<->este cruzado (externo <-> Memoria) ---------
    program(0, 0, 0, make_routing_config_instr_c<HETERO_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_FROM_E));

    // -- Scalar (1,0): reg0=100; out_E = reg0 + WEST -----------------------
    {
        HeteroInstr_C mov100;
        mov100.opcode = OP_MOV; mov100.src_a = SRC_IMM; mov100.imm = 100; mov100.dst = DST_REG; mov100.reg_dst = 0;
        HeteroInstr_C add_west;
        add_west.opcode = OP_ADD; add_west.src_a = SRC_REG; add_west.reg_a = 0; add_west.src_b = SRC_WEST;
        add_west.dst = DST_EAST;
        program(1, 0, 0, mov100);
        program(1, 0, 1, add_west);
    }

    // -- Vector (1,1): out_E = WEST + 5 -------------------------------------
    {
        HeteroInstr_C add5;
        add5.opcode = OP_ADD; add5.src_a = SRC_WEST; add5.src_b = SRC_IMM; add5.imm = 5; add5.dst = DST_EAST;
        program(1, 1, 0, add5);
    }

    in_W[0][0] = 42;  // hacia Routing -> Memoria
    in_W[1][0] = 7;   // hacia Scalar

    for (int i = 0; i < 2; i++) step(false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

    // -- Memoria (0,1): contexto 0 = NoC(oeste)->SRAM[0], 1 palabra --------
    program(0, 1, 0, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_DST_ADDR, 0));
    program(0, 1, 0, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_COUNT, 1));
    program(0, 1, 0, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
    program(0, 1, 0, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_DIR, 1));  // NoC(W)->SRAM
    program(0, 1, 0, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_START, 0));

    for (int i = 0; i < 6; i++) step(false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

    bool pass_scalar = (out_E[1][0].to_int() == 112);
    printf("%s pipeline Scalar->Vector: out_E(fila 1)  esperado=112 obtenido=%d\n",
           pass_scalar ? "PASS" : "FAIL", out_E[1][0].to_int());
    ok = ok && pass_scalar;

    // -- Reprogramar memoria: contexto 1 = SRAM[0]->NoC(oeste) -------------
    program(0, 1, 1, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_SRC_ADDR, 0));
    program(0, 1, 1, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_COUNT, 1));
    program(0, 1, 1, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
    program(0, 1, 1, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_DIR, 0));  // SRAM->NoC(W)
    program(0, 1, 1, make_memory_field_instr_c<HETERO_DATA_W>(MEM_FIELD_START, 1));

    for (int i = 0; i < 6; i++) step(false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

    bool pass_roundtrip = (out_W[0][0].to_int() == 42);
    printf("%s round trip Routing<->Memoria: out_W(fila 0)  esperado=42 obtenido=%d\n",
           pass_roundtrip ? "PASS" : "FAIL", out_W[0][0].to_int());
    ok = ok && pass_roundtrip;

    if (ok) {
        printf("\nPASS: CGRA_Hetero_2x2_Demo_Top_C (malla heterogenea real via el top de Vitis HLS)"
               " resuelve el round trip de memoria y el pipeline escalar->vectorial.\n");
    }
    return ok ? 0 : 1;
}
