// CGRA_Mesh_2x2_Heterogeneous_HLS_C__TB.cpp
// Testbench plano (sin sc_main) de la malla heterogenea generalizada
// (CGRA_Mesh_Static_C.h): 2x2 con los 4 tipos de celda nuevos/existentes,
// mismo layout que el precedente SystemC
// (CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp / CGRA_Mesh_Static_2x2_
// Heterogeneous_Test__TB.cpp):
//
//   P00 = Routing_Cell    P01 = PE_Memory
//   P10 = PE_Scalar       P11 = PE_Vector
//
// Escenario: un valor entra por el borde oeste externo (fila 0), la celda
// de enrutamiento lo desvia hacia la celda de memoria (DMA NoC(oeste)->SRAM),
// despues se reprograma la memoria para el viaje de vuelta (DMA
// SRAM->NoC(oeste)) y el enrutador lo saca de nuevo al borde oeste externo
// -- prueba el "round trip" Routing<->Memory. En paralelo, en la otra
// diagonal, un valor entra por el borde oeste externo (fila 1), la PE
// escalar lo combina con una constante y lo manda al este; la PE vectorial
// lo recibe, le suma otra constante y lo saca por el borde este externo --
// prueba el pipeline Scalar->Vector.
#include <cstdio>
#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"        // no se usa MAC aca, pero define cell_step/etc genericos de referencia
#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../pe_hls_c/vector/PE_Vector_HLS_C.h"
#include "../pe_hls_c/routing/Routing_Cell_HLS_C.h"
#include "../memory_hls_c/PE_Memory_HLS_C.h"
#include "CGRA_Mesh_Static_C.h"

static const int DATA_W = 32, VLEN = 1, ROWS = 2, COLS = 2;
static const int SIZE_WORDS = 64;

typedef Routing_Cell_State<DATA_W, VLEN>          RoutingCell;
typedef PE_Memory_State<DATA_W, VLEN, SIZE_WORDS> MemoryCell;
typedef PE_Scalar_State<DATA_W, VLEN, 8, 4>        ScalarCell;
typedef PE_Vector_State<DATA_W, VLEN, 8, 4>        VectorCell;

typedef CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, RoutingCell, MemoryCell, ScalarCell, VectorCell> HeteroMesh;

static void step_n(HeteroMesh& mesh, int n, const PE_VectorData<DATA_W, VLEN> in_N[COLS],
                    const PE_VectorData<DATA_W, VLEN> in_S[COLS], const PE_VectorData<DATA_W, VLEN> in_W[ROWS],
                    const PE_VectorData<DATA_W, VLEN> in_E[ROWS]) {
    for (int i = 0; i < n; i++) mesh_step(mesh, /*rst=*/false, /*enable=*/true, in_N, in_S, in_W, in_E);
}

int main() {
    bool ok = true;
    HeteroMesh mesh;

    PE_VectorData<DATA_W, VLEN> zero[2];
    mesh_step(mesh, /*rst=*/true, true, zero, zero, zero, zero);  // alinea pc/config_bank/FSM de las 4 celdas

    // -- Routing (0,0): oeste<->este cruzado (externo <-> Memoria) ---------
    mesh_program(mesh, 0, 0, /*ctx=*/0, make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_FROM_E));

    // -- Scalar (1,0): reg0=100; out_E = reg0 + WEST -----------------------
    {
        PE_Instruction<DATA_W> mov100;
        mov100.opcode = OP_MOV; mov100.src_a = SRC_IMM; mov100.imm = 100; mov100.dst = DST_REG; mov100.reg_dst = 0;
        PE_Instruction<DATA_W> add_west;
        add_west.opcode = OP_ADD; add_west.src_a = SRC_REG; add_west.reg_a = 0; add_west.src_b = SRC_WEST;
        add_west.dst = DST_EAST;
        mesh_program(mesh, 1, 0, 0, mov100);
        mesh_program(mesh, 1, 0, 1, add_west);
    }

    // -- Vector (1,1): out_E = WEST + 5 -------------------------------------
    {
        PE_Instruction<DATA_W> add5;
        add5.opcode = OP_ADD; add5.src_a = SRC_WEST; add5.src_b = SRC_IMM; add5.imm = 5; add5.dst = DST_EAST;
        mesh_program(mesh, 1, 1, 0, add5);
    }

    PE_VectorData<DATA_W, VLEN> in_N[COLS], in_S[COLS], in_W[ROWS], in_E[ROWS];
    in_W[0][0] = 42;  // hacia Routing -> Memoria
    in_W[1][0] = 7;   // hacia Scalar

    // "Cebar" el enrutamiento antes de disparar la rafaga de memoria: el
    // valor externo (in_W) llega a Routing.out_E de inmediato (borde,
    // lectura sin retardo), pero Memoria solo lo ve un ciclo despues (via el
    // snapshot "viejo" de mesh_step -- misma disciplina que ya obligo a
    // "presentar la fase un ciclo antes" en cgra_run para GEMM). Con una
    // rafaga de 1 sola palabra en modo DIRECT no hay una segunda oportunidad
    // de transferencia si el primer ciclo todavia ve el valor viejo (0) --
    // por eso se prima el enrutador ANTES de programar/disparar la rafaga.
    step_n(mesh, 2, in_N, in_S, in_W, in_E);

    // -- Memoria (0,1): contexto 0 = NoC(oeste)->SRAM[0], 1 palabra --------
    mesh_program(mesh, 0, 1, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DST_ADDR, 0));
    mesh_program(mesh, 0, 1, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_COUNT, 1));
    mesh_program(mesh, 0, 1, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
    mesh_program(mesh, 0, 1, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DIR, 1));  // NoC(W)->SRAM
    mesh_program(mesh, 0, 1, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_START, 0));

    step_n(mesh, 6, in_N, in_S, in_W, in_E);

    RoutingCell& routing = mesh.cell<0, 0>();
    MemoryCell& memory   = mesh.cell<0, 1>();
    ScalarCell& scalar   = mesh.cell<1, 0>();
    VectorCell& vector_c = mesh.cell<1, 1>();

    bool pass_sram = (memory.done && !memory.busy && memory.sram[0].to_int() == 42);
    printf("%s memoria: sram[0] = in_W(externo) via Routing  esperado=42 obtenido=%d (done=%d)\n",
           pass_sram ? "PASS" : "FAIL", memory.sram[0].to_int(), memory.done ? 1 : 0);
    ok = ok && pass_sram;

    // -- Reprogramar memoria: contexto 1 = SRAM[0]->NoC(oeste) -------------
    mesh_program(mesh, 0, 1, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_SRC_ADDR, 0));
    mesh_program(mesh, 0, 1, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_COUNT, 1));
    mesh_program(mesh, 0, 1, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
    mesh_program(mesh, 0, 1, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DIR, 0));  // SRAM->NoC(W)
    mesh_program(mesh, 0, 1, /*ctx=*/1, make_memory_field_instr_c<DATA_W>(MEM_FIELD_START, 1));

    step_n(mesh, 6, in_N, in_S, in_W, in_E);

    PE_VectorData<DATA_W, VLEN> all_out_N[ROWS][COLS], all_out_S[ROWS][COLS];
    PE_VectorData<DATA_W, VLEN> all_out_E[ROWS][COLS], all_out_W[ROWS][COLS];
    mesh_read_outputs(mesh, all_out_N, all_out_S, all_out_E, all_out_W);

    bool pass_roundtrip = (all_out_W[0][0][0].to_int() == 42);
    printf("%s round trip Routing<->Memoria: out_W(externo, fila 0)  esperado=42 obtenido=%d\n",
           pass_roundtrip ? "PASS" : "FAIL", all_out_W[0][0][0].to_int());
    ok = ok && pass_roundtrip;

    bool pass_scalar = (scalar.reg_file[0].to_int() == 100);
    printf("%s escalar: reg0 = 100 (MOV_IMM)  esperado=100 obtenido=%d\n",
           pass_scalar ? "PASS" : "FAIL", scalar.reg_file[0].to_int());
    ok = ok && pass_scalar;

    bool pass_vector = (all_out_E[1][1][0].to_int() == 112);
    printf("%s pipeline Scalar->Vector: out_E(externo, fila 1)  esperado=112 obtenido=%d\n",
           pass_vector ? "PASS" : "FAIL", all_out_E[1][1][0].to_int());
    ok = ok && pass_vector;

    (void)routing; (void)vector_c;

    if (ok) {
        printf("\nPASS: malla heterogenea 2x2 (Routing+Memoria+Scalar+Vector) -- round trip de\n"
               "memoria via enrutamiento y pipeline escalar->vectorial, ambos correctos.\n");
    }
    return ok ? 0 : 1;
}
