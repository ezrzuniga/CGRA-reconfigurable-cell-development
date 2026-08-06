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

    // --- Contextos: programa el contexto 1 con un programa DISTINTO
    // (reg0=99 en vez de 5) y confirma que activarlo (programar cualquier
    // slot de ese contexto ya lo activa, ver pe_scalar_program) no pisa lo
    // que quedo grabado en el contexto 0.
    unsigned ctx1_slot0 = 1 * INSTR_MEM_SIZE + 0;  // ctx=1, addr=0
    pe_scalar_program(s, ctx1_slot0, mov_imm(99, DST_REG, 0));
    pe_scalar_step(s, /*rst=*/true, /*enable=*/true, in_N, in_S, in_E, in_W);  // pc->0, ctx activo=1
    pe_scalar_step(s, false, true, in_N, in_S, in_E, in_W);                    // ejecuta slot0 del ctx1
    bool pass_ctx1 = (s.reg_file[0].to_int() == 99);
    printf("%s contexto 1 activo tras programarlo: reg0=99 esperado=99 obtenido=%d\n",
           pass_ctx1 ? "PASS" : "FAIL", s.reg_file[0].to_int());
    ok = ok && pass_ctx1;

    // --- Bucle HW + predicado: contexto 2 con
    //   slot0: OP_LOOP b=3        (repite 3 veces el cuerpo)
    //   slot1: reg2 = reg2 + 1    (cuerpo del bucle)
    //   slot2: OP_ENDLOOP         (cierra el bucle, salta a slot1 si quedan iteraciones)
    //   slot3: reg3 = 111, con pred_gate=1 (solo se escribe si pred==true)
    State s2;
    unsigned ctx = 2;
    Instr loop_setup; loop_setup.opcode = OP_LOOP; loop_setup.src_b = SRC_IMM; loop_setup.imm = 3;
    Instr loop_body;  // reg2 = reg2 + 1, cada iteracion
    loop_body.opcode = OP_ADD; loop_body.src_a = SRC_REG; loop_body.reg_a = 2;
    loop_body.src_b = SRC_IMM; loop_body.imm = 1; loop_body.dst = DST_REG; loop_body.reg_dst = 2;
    Instr loop_end; loop_end.opcode = OP_ENDLOOP;
    Instr pred_write = mov_imm(111, DST_REG, 3); pred_write.pred_gate = 1;

    pe_scalar_program(s2, ctx * INSTR_MEM_SIZE + 0, loop_setup);
    pe_scalar_program(s2, ctx * INSTR_MEM_SIZE + 1, loop_body);
    pe_scalar_program(s2, ctx * INSTR_MEM_SIZE + 2, loop_end);
    pe_scalar_program(s2, ctx * INSTR_MEM_SIZE + 3, pred_write);

    // Traza pc por ciclo (verificada a mano): rst->pc=0.
    //   step0 (LOOP,pc0->1) step1(body,pc1->2) step2(ENDLOOP,pc2->1,iters3->2)
    //   step3(body,pc1->2)  step4(ENDLOOP,pc2->1,iters2->1) step5(body,pc1->2)
    //   step6(ENDLOOP,pc2->3,iters=1 no repite mas,exit) step7(pred_write EN pc3)
    // Se necesitan 8 pasos para que slot3 (pred_write) efectivamente CORRA
    // (los primeros 7 solo llegan hasta pc=3, recien el 8vo lo ejecuta).
    PE_VectorData<DATA_W, VLEN> z;
    pe_scalar_step(s2, /*rst=*/true, /*enable=*/true, z, z, z, z);  // pc->0, activa ctx2
    for (int i = 0; i < 8; i++) pe_scalar_step(s2, false, true, z, z, z, z);
    bool pass_loop = (s2.reg_file[2].to_int() == 3);
    printf("%s bucle HW ejecuta el cuerpo 3 veces: reg2 esperado=3 obtenido=%d\n",
           pass_loop ? "PASS" : "FAIL", s2.reg_file[2].to_int());
    ok = ok && pass_loop;

    // pred sigue en false (nunca se corrio OP_PSET) -> pred_write (que SI se
    // ejecuto, en el paso 8) no debe haber escrito reg3.
    bool pass_pred_blocked = (s2.reg_file[3].to_int() == 0);
    printf("%s escritura con pred_gate=1 bloqueada (pred=false): reg3 esperado=0 obtenido=%d\n",
           pass_pred_blocked ? "PASS" : "FAIL", s2.reg_file[3].to_int());
    ok = ok && pass_pred_blocked;

    // OP_PSET pone pred=true -> reejecutando pred_write (via slot0) ahora si
    // debe escribir reg3.
    Instr pset; pset.opcode = OP_PSET; pset.src_a = SRC_IMM; pset.imm = 1;
    pe_scalar_program(s2, ctx * INSTR_MEM_SIZE + 0, pset);        // slot0 ahora es PSET (ya no LOOP)
    pe_scalar_program(s2, ctx * INSTR_MEM_SIZE + 1, pred_write);  // slot1 = escritura predicada
    s2.pc = 0;
    pe_scalar_step(s2, false, true, z, z, z, z);  // corre PSET: pred=true, pc->1
    pe_scalar_step(s2, false, true, z, z, z, z);  // corre pred_write con pred=true
    bool pass_pred_open = (s2.reg_file[3].to_int() == 111);
    printf("%s escritura con pred_gate=1 permitida (pred=true): reg3 esperado=111 obtenido=%d\n",
           pass_pred_open ? "PASS" : "FAIL", s2.reg_file[3].to_int());
    ok = ok && pass_pred_open;

    if (ok) printf("\nPASS: PE_Scalar_HLS_C resuelve el programa de prueba y difunde a los %d lanes.\n", VLEN);
    return ok ? 0 : 1;
}
