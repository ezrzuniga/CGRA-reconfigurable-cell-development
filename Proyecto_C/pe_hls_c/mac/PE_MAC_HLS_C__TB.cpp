// PE_MAC_HLS_C__TB.cpp
// Transliteracion a C/C++ puro de pe_hls/mac/PE_MAC_HLS__TB.cpp. Caso critico:
// confirma que el acumulador sigue sumando correctamente con operandos
// repetidos ciclo a ciclo (una suma por ciclo, ni de mas ni de menos), y que
// rst NO lo limpia mientras que DST_ACC si.
//
// El "workaround clk.neg()" que motivaba el TB original ya no tiene ni sentido
// aca: en C no hay procesos ni delta-ciclos, writeback() es una llamada mas
// dentro de pe_mac_step(). Lo que si sigue teniendo sentido es la propiedad que
// ese workaround protegia -- que el acumulador avance exactamente un paso por
// ciclo habilitado -- y es lo que se verifica lane a lane.
//
// INSTR_MEM_SIZE=1: una sola instruccion, el pc siempre vuelve a la direccion 0
// y la MAC corre en todos los ciclos.

#include <cstdio>
#include "PE_MAC_HLS_C.h"
#include "../test_util_c.h"

static const int DATA_W = 32;
static const int VLEN   = 4;
typedef PE_MAC_State<DATA_W, VLEN, 8, 1> Pe;
typedef PE_VectorData<DATA_W, VLEN>      Link;
typedef PE_Instruction<DATA_W>           Instr;

static Link link4(int32_t a, int32_t b, int32_t c, int32_t d) {
    Link v; v[0] = a; v[1] = b; v[2] = c; v[3] = d; return v;
}

static Link lane_delta(const Link& before, const Link& after) {
    Link d;
    for (int i = 0; i < VLEN; ++i) d[i] = after[i] - before[i];
    return d;
}

int main() {
    Pe pe;
    Link in_N, in_S, in_E, in_W;
    bool ok = true;

    // Reset inicial (alinea pc; no toca acc/reg_file/instr_mem).
    pe_mac_step(pe, /*rst=*/true, /*enable=*/false, in_N, in_S, in_E, in_W);
    test_count_cycles(1);

    in_W = link4(1, 2, 3, 4);

    Instr mac_instr;
    mac_instr.opcode = OP_MAC;
    mac_instr.src_a = SRC_WEST;
    mac_instr.src_b = SRC_IMM;
    mac_instr.imm = 5;
    mac_instr.dst = DST_EAST;
    pe_mac_program(pe, /*slot=*/0, mac_instr);   // canal lateral: 0 ciclos

    const Link expected_delta = link4(5, 10, 15, 20);

    test_section("Acumulacion: un paso de acc por ciclo habilitado");
    Link prev = pe.out_E;
    for (int i = 0; i < 3; ++i) {
        pe_mac_step(pe, false, true, in_N, in_S, in_E, in_W);
        test_count_cycles(1);
        Link cur = pe.out_E;
        test_check_link(ok, "delta de 1 ciclo (no 3)", "in_W=[1,2,3,4], imm=5",
                        expected_delta, lane_delta(prev, cur));
        prev = cur;
    }

    test_section("enable=false congela el acumulador");
    for (int i = 0; i < 3; ++i) {
        pe_mac_step(pe, false, /*enable=*/false, in_N, in_S, in_E, in_W);
        test_count_cycles(1);
    }
    test_check_link(ok, "out_E no cambio con enable=false", "3 ciclos deshabilitados", prev, pe.out_E);

    test_section("Reanudar: la acumulacion sigue exactamente donde quedo");
    Link frozen = pe.out_E;
    pe_mac_step(pe, false, true, in_N, in_S, in_E, in_W);
    test_count_cycles(1);
    test_check_link(ok, "delta tras reanudar", "enable vuelve a true",
                    expected_delta, lane_delta(frozen, pe.out_E));

    test_section("rst NO limpia el acumulador");
    Link before_rst = pe.out_E;
    pe_mac_step(pe, /*rst=*/true, true, in_N, in_S, in_E, in_W);
    test_count_cycles(1);
    pe_mac_step(pe, false, true, in_N, in_S, in_E, in_W);
    test_count_cycles(1);
    test_check_link(ok, "la acumulacion continua sin interrupcion tras un pulso de rst",
                    "rst=1 un ciclo", expected_delta, lane_delta(before_rst, pe.out_E));

    test_section("DST_ACC si limpia el acumulador, y SRC_ACC lo confirma");
    Instr clear_instr;
    clear_instr.opcode = OP_MOV;
    clear_instr.src_a = SRC_IMM;
    clear_instr.imm = 0;
    clear_instr.dst = DST_ACC;
    pe_mac_program(pe, 0, clear_instr);
    pe_mac_step(pe, false, true, in_N, in_S, in_E, in_W);
    test_count_cycles(1);

    Instr read_acc_instr;
    read_acc_instr.opcode = OP_MOV;
    read_acc_instr.src_a = SRC_ACC;
    read_acc_instr.dst = DST_EAST;
    pe_mac_program(pe, 0, read_acc_instr);
    pe_mac_step(pe, false, true, in_N, in_S, in_E, in_W);
    test_count_cycles(1);

    test_check_link(ok, "SRC_ACC lee 0 despues de un MOV imm=0 -> DST_ACC", "clear via DST_ACC",
                    Link(), pe.out_E);

    // pe_mac_clear_acc() es el otro camino (canal lateral directo, sin gastar
    // una instruccion) -- no existia en el original, donde limpiar el
    // acumulador obligaba a pisar instr_mem.
    test_section("pe_mac_clear_acc(): mismo efecto sin gastar una instruccion");
    pe_mac_program(pe, 0, mac_instr);
    pe_mac_step(pe, false, true, in_N, in_S, in_E, in_W);
    test_count_cycles(1);
    pe_mac_clear_acc(pe);
    pe_mac_program(pe, 0, read_acc_instr);
    pe_mac_step(pe, false, true, in_N, in_S, in_E, in_W);
    test_count_cycles(1);
    test_check_link(ok, "SRC_ACC lee 0 despues de pe_mac_clear_acc()", "clear por canal lateral",
                    Link(), pe.out_E);

    if (ok) {
        printf("\nPASS: PE_MAC_HLS_C acumula un paso por ciclo, congela con enable=0, "
               "sobrevive a rst y se limpia solo por DST_ACC / pe_mac_clear_acc().\n");
    }
    return ok ? 0 : 1;
}
