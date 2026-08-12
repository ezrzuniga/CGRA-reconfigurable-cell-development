// PE_Memory_HLS_C__TB.cpp
// Testbench plano de PE_Memory_HLS_C.h. Cubre las TRES direcciones de rafaga
// que soporta la celda, en los dos modos de direccionamiento:
//
//   1. dir=0 STRIDE  : SRAM -> NoC(oeste), 3 palabras consecutivas, una por
//                      ciclo (este caso no lo cubria el TB SystemC, que solo
//                      usaba MODE_DIRECT de 1 palabra).
//   2. dir=1 DIRECT  : NoC(oeste) -> SRAM  (la ida del round trip que si hacia
//                      memory_hls/PE_Memory_HLS_Cell__TB.cpp).
//   3. dir=2 DIRECT  : SRAM -> SRAM        (copia interna; no la ejercitaba
//                      ningun testbench del arbol SystemC).
//
// El round trip completo NoC->SRAM->NoC a traves de la malla, que era el otro
// escenario del TB SystemC, se sigue cubriendo en
// mesh_hls_c/CGRA_Mesh_2x2_Heterogeneous_HLS_C__TB.cpp con la celda de
// enrutamiento de por medio.
#include <cstdio>
#include "PE_Memory_HLS_C.h"

static const int DATA_W = 32, VLEN = 1, SIZE_WORDS = 64;
typedef PE_Memory_State<DATA_W, VLEN, SIZE_WORDS> State;

int main() {
    bool ok = true;

    State s;
    s.sram[0] = 11; s.sram[1] = 22; s.sram[2] = 33;

    memory_program(s, 0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_SRC_ADDR, 0));
    memory_program(s, 0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_STRIDE, 1));
    memory_program(s, 0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_COUNT, 3));
    memory_program(s, 0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_MODE, AccessController::MODE_STRIDE));
    memory_program(s, 0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DIR, 0));  // SRAM->NoC(W)
    memory_program(s, 0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_START, 0));

    bool pass_busy = s.busy && !s.done;
    printf("%s busy tras START\n", pass_busy ? "PASS" : "FAIL");
    ok = ok && pass_busy;

    int32_t expected[3] = {11, 22, 33};
    PE_VectorData<DATA_W, VLEN> in_N, in_S, in_E, in_W;
    for (int i = 0; i < 3; i++) {
        memory_step(s, /*rst=*/false, /*enable=*/true, in_N, in_S, in_E, in_W);
        bool pass = (s.out_W[0].to_int() == expected[i]);
        printf("%s out_W ciclo %d  esperado=%d obtenido=%d\n", pass ? "PASS" : "FAIL", i, expected[i], s.out_W[0].to_int());
        if (!pass) ok = false;
    }

    memory_step(s, false, true, in_N, in_S, in_E, in_W);  // 4to ciclo: rafaga se cierra
    bool pass_done = (s.done && !s.busy);
    printf("%s done=true, busy=false tras agotar la rafaga\n", pass_done ? "PASS" : "FAIL");
    ok = ok && pass_done;

    //=======================================================================
    // 2) dir=1 (NoC oeste -> SRAM), MODE_DIRECT, 1 palabra: la ida del round
    //    trip que hacia memory_hls/PE_Memory_HLS_Cell__TB.cpp.
    //=======================================================================
    memory_program(s, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DST_ADDR, 10));
    memory_program(s, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_COUNT, 1));
    memory_program(s, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
    memory_program(s, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DIR, 1));  // NoC(W)->SRAM
    memory_program(s, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_START, 0));

    in_W[0] = 100;                                        // el vecino oeste presenta el dato
    memory_step(s, false, true, in_N, in_S, in_E, in_W);  // transfiere la palabra
    memory_step(s, false, true, in_N, in_S, in_E, in_W);  // cierra la rafaga
    bool pass_in = (s.sram[10].to_int() == 100 && s.done && !s.busy);
    printf("%s dir=1 NoC(oeste)->SRAM  sram[10] esperado=100 obtenido=%d (done=%d)\n",
           pass_in ? "PASS" : "FAIL", s.sram[10].to_int(), s.done ? 1 : 0);
    ok = ok && pass_in;

    //=======================================================================
    // 3) dir=2 (SRAM -> SRAM), MODE_DIRECT: copia interna, sin tocar el puerto
    //    NoC. No la ejercitaba ningun testbench del arbol SystemC.
    //=======================================================================
    memory_program(s, /*ctx=*/2, make_memory_field_instr_c<DATA_W>(MEM_FIELD_SRC_ADDR, 10));
    memory_program(s, /*ctx=*/2, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DST_ADDR, 20));
    memory_program(s, /*ctx=*/2, make_memory_field_instr_c<DATA_W>(MEM_FIELD_COUNT, 1));
    memory_program(s, /*ctx=*/2, make_memory_field_instr_c<DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
    memory_program(s, /*ctx=*/2, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DIR, 2));  // SRAM->SRAM
    memory_program(s, /*ctx=*/2, make_memory_field_instr_c<DATA_W>(MEM_FIELD_START, 0));

    memory_step(s, false, true, in_N, in_S, in_E, in_W);
    memory_step(s, false, true, in_N, in_S, in_E, in_W);
    bool pass_copy = (s.sram[20].to_int() == 100 && s.done && !s.busy);
    printf("%s dir=2 SRAM->SRAM  sram[20] esperado=100 obtenido=%d (done=%d)\n",
           pass_copy ? "PASS" : "FAIL", s.sram[20].to_int(), s.done ? 1 : 0);
    ok = ok && pass_copy;

    //=======================================================================
    // 4) rst limpia solo la FSM, NO la SRAM ni los contextos (mismo precedente
    //    que el pc en las celdas tipo PE).
    //=======================================================================
    memory_step(s, /*rst=*/true, true, in_N, in_S, in_E, in_W);
    bool pass_rst = (!s.busy && !s.done && s.sram[20].to_int() == 100);
    printf("%s rst limpia la FSM pero conserva la SRAM (sram[20] sigue en 100)\n",
           pass_rst ? "PASS" : "FAIL");
    ok = ok && pass_rst;

    if (ok) printf("\nPASS: PE_Memory_HLS_C cubre las 3 direcciones de rafaga "
                   "(SRAM->NoC en stride, NoC->SRAM, SRAM->SRAM) y conserva la SRAM ante rst.\n");
    return ok ? 0 : 1;
}
