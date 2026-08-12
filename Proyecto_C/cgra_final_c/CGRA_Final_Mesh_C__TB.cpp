// CGRA_Final_Mesh_C__TB.cpp
// Transliteracion a C/C++ puro de cgra_final/CGRA_Final_Mesh__TB.cpp: mismo
// smoke test estructural (mismos estimulos, mismos valores esperados, mismos
// puertos de lectura), sin sc_main/sc_clock/sc_signal.
//
// No valida un algoritmo (GEMM/FFT/SoftMax quedan para cgra_final_TB_c/),
// solo que la malla 3x3 completa elabora, cablea correctamente y que cada uno
// de los 5 tipos de celda es alcanzable y programable en su posicion real del
// layout, usando siempre un puerto de borde real de la malla para poder
// observar el resultado sin depender de ninguna otra celda intermedia:
//
//   Memoria (0,0): precarga de sram[5]=42, rafaga directa (ctx0, dir=0) y
//                  lectura por out_W[0] (borde real oeste).
//   Vectorial(0,1): MOV imm->DST_NORTH, lectura por out_N[1] (borde real norte).
//   Escalar (0,2):  MOV imm->DST_EAST, lectura por out_E[0] (borde real este).
//   Routing (2,0):  contexto sel_S=RC_FROM_W, estimulo en in_W[2], lectura
//                   por out_S[0] (celda con dos bordes reales).
//   MAC (2,2):      un unico OP_MAC(imm,imm)->DST_EAST (el destino de un
//                   OP_MAC recibe el acumulador ya actualizado), lectura por
//                   out_E[2] (borde real este, fila 2).
//
// Tres diferencias de mecanica respecto del original SystemC, todas a favor
// de la version C (ninguna cambia lo que se verifica):
//
//   1. Programar no consume ciclos. load_instr() escribia en un sc_signal que
//      la celda muestreaba en el flanco siguiente, asi que cada carga costaba
//      un advance_cycles(1) y de paso corria el pc de TODAS las celdas.
//      mesh_program() es una escritura directa a instr_mem/config_bank (canal
//      lateral, ver PE_MAC_HLS_C.h): la secuencia de 6 campos de la celda de
//      memoria, que en el original ocupaba 6 ciclos, aca ocupa 0.
//   2. Por lo mismo, clear_instr() no tiene equivalente ni hace falta: no hay
//      un puerto instr_in que quede "pegado" pidiendo recargar la misma
//      instruccion ciclo tras ciclo.
//   3. Precargar la SRAM es `mesh.cell<0,0>().sram[5] = 42` -- el write_sram()
//      del original existia solo porque sram era privado de un sc_module.

#include <cstdio>
#include "CGRA_Final_Mesh_C.h"
#include "../pe_hls_c/test_util_c.h"

typedef CGRA_Final_Mesh_C Mesh;
typedef CGRA_Final_Link   Link;
typedef CGRA_Final_Instr  Instr;

static const int ROWS = CGRA_FINAL_ROWS;
static const int COLS = CGRA_FINAL_COLS;
static const int DATA_W = CGRA_FINAL_DATA_W;

// Bordes de entrada de la malla, persistentes entre ciclos (el equivalente de
// los sc_signal que el testbench original manejaba: un wire escrito una vez
// se queda manejado hasta que alguien lo reescriba).
static Link g_in_N[COLS], g_in_S[COLS], g_in_W[ROWS], g_in_E[ROWS];

static void step_n(Mesh& mesh, int n, bool rst = false) {
    for (int i = 0; i < n; i++) cgra_final_step(mesh, rst, /*enable=*/true, g_in_N, g_in_S, g_in_W, g_in_E);
    test_count_cycles(n);
}

static Link link4(int32_t a, int32_t b, int32_t c, int32_t d) {
    Link v; v[0] = a; v[1] = b; v[2] = c; v[3] = d; return v;
}

static Instr mov_imm_instr(int32_t imm, ap_uint<3> dst) {
    Instr i;
    i.opcode = OP_MOV;
    i.src_a = SRC_IMM;
    i.imm = imm;
    i.dst = dst;
    return i;
}

static Instr mac_imm_instr(int32_t imm, ap_uint<3> dst) {
    Instr i;
    i.opcode = OP_MAC;
    i.src_a = SRC_IMM;
    i.src_b = SRC_IMM;
    i.imm = imm;
    i.dst = dst;
    return i;
}

int main() {
    Mesh mesh;
    bool ok = true;

    test_section("Reset");
    step_n(mesh, 1, /*rst=*/true);
    printf("layout 3x3: (0,0)=Memoria (0,1)=Vectorial (0,2)=Escalar / "
           "(1,0)=Routing (1,1)=MAC (1,2)=MAC / (2,0)=Routing (2,1)=MAC (2,2)=MAC\n");

    // Acceso tipado directo por (fila,columna) en tiempo de compilacion.
    CGRA_Final_MemCell& mem_cell = mesh.cell<0, 0>();
    CGRA_Final_VecCell& vec_cell = mesh.cell<0, 1>();
    CGRA_Final_ScaCell& sca_cell = mesh.cell<0, 2>();
    CGRA_Final_RouCell& rou_cell = mesh.cell<2, 0>();
    CGRA_Final_MacCell& mac_cell = mesh.cell<2, 2>();

    //======================================================================
    // Memoria (0,0): precarga sram[5]=42, rafaga directa SRAM->NoC, lee out_W[0]
    //======================================================================
    test_section("Memoria (0,0): precarga sram[5]=42 y rafaga directa (ctx0, dir=0)");
    mem_cell.sram[5] = 42;
    mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_SRC_ADDR, 5));
    mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DST_ADDR, 0));
    mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
    mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DIR, 0));
    mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_COUNT, 1));
    mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_START, 1));

    bool mem_done = false;
    for (int i = 0; i < 10 && !mem_done; i++) {
        step_n(mesh, 1);
        mem_done = mem_cell.done;
    }
    test_check_bool(ok, "Memoria: DMA directo SRAM->NoC completo", "sram[5]=42", mem_done);
    test_check(ok, "Memoria: borde W real == sram[5]", "sram[5]=42, dir=0",
               42, mem_cell.out_W[0].to_int());

    //======================================================================
    // Routing (2,0): sel_S = RC_FROM_W, estimulo en in_W[2], lee out_S[0]
    //======================================================================
    test_section("Routing (2,0): ctx0 sel_S=RC_FROM_W");
    mesh_program(mesh, 2, 0, /*ctx=*/0,
                 make_routing_config_instr_c<DATA_W>(RC_NONE, RC_FROM_W, RC_NONE, RC_NONE));

    g_in_W[2] = link4(77, 77, 77, 77);
    // Un unico ciclo alcanza: el borde externo se ve en el mismo mesh_step()
    // en que se escribe (a diferencia de un sc_signal escrito desde sc_main),
    // y la celda de Routing lo mux-ea a out_S en ese mismo ciclo.
    step_n(mesh, 1);
    test_check_link(ok, "Routing: borde S real == relay de W(real)",
                    "in_W[2]=" + link_to_string(g_in_W[2]), link4(77, 77, 77, 77), rou_cell.out_S);

    //======================================================================
    // Vectorial (0,1): MOV imm=17 -> DST_NORTH, lee out_N[1]
    //======================================================================
    test_section("Vectorial (0,1): MOV imm=17 -> DST_NORTH");
    mesh_program(mesh, 0, 1, /*slot=*/0, mov_imm_instr(17, DST_NORTH));

    // El pc de esta celda viene corriendo libre desde el reset inicial. En vez
    // de forzar un pulso de rst para realinearlo a 0, se espera una vuelta
    // completa de INSTR_MEM_SIZE ciclos: el pc pasa por la direccion 0 (ya
    // cargada) exactamente una vez, ejecuta el MOV, y el resultado queda
    // estable el resto de la ventana -- mismo margen que usaba el original.
    step_n(mesh, CGRA_FINAL_INSTR_MEM_SIZE);
    test_check_link(ok, "Vectorial: borde N real == imm difundido a las 4 lanes", "imm=17",
                    link4(17, 17, 17, 17), vec_cell.out_N);

    //======================================================================
    // Escalar (0,2): MOV imm=23 -> DST_EAST, lee out_E[0]
    //======================================================================
    test_section("Escalar (0,2): MOV imm=23 -> DST_EAST");
    mesh_program(mesh, 0, 2, /*slot=*/0, mov_imm_instr(23, DST_EAST));

    // Aca si conviene el pulso de rst (mismo truco que el original): realinea
    // el pc de todas las celdas a 0 sin tocar reg_file/acc/instr_mem, asi que
    // el ciclo siguiente ejecuta exactamente la direccion 0 recien cargada.
    // Efecto colateral heredado del original: rst tambien limpia el banco de
    // contextos de Routing -- por eso este caso va DESPUES del de Routing.
    step_n(mesh, 1, /*rst=*/true);
    step_n(mesh, 1);
    test_check_scalar(ok, "Escalar: borde E real == imm difundido (broadcast escalar->vector)", "imm=23",
                      link4(23, 23, 23, 23), sca_cell.out_E);

    //======================================================================
    // MAC (2,2): un unico OP_MAC(imm=6,imm=6) -> DST_EAST, lee out_E[2]
    //======================================================================
    test_section("MAC (2,2): OP_MAC(imm=6,imm=6) -> DST_EAST");
    mesh_program(mesh, 2, 2, /*slot=*/0, mac_imm_instr(6, DST_EAST));

    // rst + 1 ciclo: la direccion 0 (con el OP_MAC) se ejecuta exactamente una
    // vez, sin riesgo de que el acumulador sume dos veces.
    step_n(mesh, 1, /*rst=*/true);
    step_n(mesh, 1);
    test_check_link(ok, "MAC: borde E real == acc tras un OP_MAC(6,6)", "imm=6",
                    link4(36, 36, 36, 36), mac_cell.out_E);  // acc = 0 + 6*6

    if (ok) {
        printf("\nPASS: CGRA_Final_Mesh_C 3x3 (Memoria/Vectorial/Escalar/Routing/MAC) "
               "elabora y cada tipo de celda es programable en su posicion real.\n");
    }
    return ok ? 0 : 1;
}
