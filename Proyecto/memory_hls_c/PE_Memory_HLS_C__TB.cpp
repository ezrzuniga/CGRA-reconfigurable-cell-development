// PE_Memory_HLS_C__TB.cpp
// Testbench plano de PE_Memory_HLS_C.h: precarga 3 palabras en SRAM,
// programa un contexto en modo STRIDE (SRAM -> NoC oeste) y verifica que la
// rafaga las transfiere una por ciclo a out_W, terminando en done=true.
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

    if (ok) printf("\nPASS: PE_Memory_HLS_C transfiere 3 palabras SRAM->NoC(oeste) y cierra la rafaga.\n");
    return ok ? 0 : 1;
}
