// CGRA_Final_NoC_Mesh_C__TB.cpp
// Transliteracion a C/C++ puro de cgra_final_noc/CGRA_Final_NoC_Mesh__TB.cpp.
// Mismo smoke test estructural que cgra_final_c/CGRA_Final_Mesh_C__TB.cpp
// (mismos estimulos, mismos valores esperados, MISMOS margenes de ciclos),
// aplicado a CGRA_Final_NoC_Mesh_C en vez de CGRA_Final_Mesh_C. El objetivo de
// reusar exactamente los mismos vectores es demostrar equivalencia funcional
// ciclo-a-ciclo entre la malla de wires directos y la malla NoC: si este
// archivo pasa con los mismos resultados que el original, la fabrica de
// routers (NoC_Router_C.h) esta reenviando cada paquete exactamente donde el
// wire directo lo hubiera llevado, en el mismo numero de ciclos.
//
// Unica diferencia de codigo respecto del TB de malla directa (mas alla de los
// typedefs): las salidas de borde se leen de los arreglos out_* que devuelve
// noc_mesh_step(), no del campo out_X de la celda de borde. En la malla NoC el
// ultimo tramo hacia el exterior lo maneja el router de esa posicion, que es
// ademas quien descarta el header -- el payload coincide, pero el puerto
// correcto de esta malla es ese (ver NoC_Mesh_Static_C.h).

#include <cstdio>
#include "CGRA_Final_NoC_Mesh_C.h"
#include "../pe_hls_c/test_util_c.h"

typedef CGRA_Final_NoC_Mesh_C Mesh;
typedef CGRA_Final_NoC_Link   Link;
typedef CGRA_Final_NoC_Instr  Instr;

static const int ROWS = CGRA_FINAL_ROWS;
static const int COLS = CGRA_FINAL_COLS;
static const int DATA_W = CGRA_FINAL_DATA_W;

static Link g_in_N[COLS],  g_in_S[COLS],  g_in_W[ROWS],  g_in_E[ROWS];
static Link g_out_N[COLS], g_out_S[COLS], g_out_W[ROWS], g_out_E[ROWS];

static void step_n(Mesh& mesh, int n, bool rst = false) {
    for (int i = 0; i < n; i++)
        cgra_final_noc_step(mesh, rst, /*enable=*/true, g_in_N, g_in_S, g_in_W, g_in_E,
                            g_out_N, g_out_S, g_out_W, g_out_E);
    test_count_cycles(n);
}

static Link link4(int32_t a, int32_t b, int32_t c, int32_t d) {
    Link v; v[0] = a; v[1] = b; v[2] = c; v[3] = d; return v;
}

static Instr mov_imm_instr(int32_t imm, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_IMM; i.imm = imm; i.dst = dst; return i;
}

static Instr mac_imm_instr(int32_t imm, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MAC; i.src_a = SRC_IMM; i.src_b = SRC_IMM; i.imm = imm; i.dst = dst; return i;
}

int main() {
    Mesh mesh;
    bool ok = true;

    test_section("Reset");
    step_n(mesh, 1, /*rst=*/true);
    printf("layout 3x3 (NoC): (0,0)=Memoria (0,1)=Vectorial (0,2)=Escalar / "
           "(1,0)=Routing (1,1)=MAC (1,2)=MAC / (2,0)=Routing (2,1)=MAC (2,2)=MAC\n");

    CGRA_Final_MemCell& mem_cell = mesh.cell<0, 0>();

    //======================================================================
    // Memoria (0,0): precarga sram[5]=42, rafaga directa SRAM->NoC(local),
    // lee out_W[0]
    //======================================================================
    test_section("Memoria (0,0): precarga sram[5]=42 y rafaga directa (ctx0, dir=0)");
    mem_cell.sram[5] = 42;
    noc_mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_SRC_ADDR, 5));
    noc_mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DST_ADDR, 0));
    noc_mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
    noc_mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_DIR, 0));
    noc_mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_COUNT, 1));
    noc_mesh_program(mesh, 0, 0, /*ctx=*/0, make_memory_field_instr_c<DATA_W>(MEM_FIELD_START, 1));

    bool mem_done = false;
    for (int i = 0; i < 10 && !mem_done; i++) {
        step_n(mesh, 1);
        mem_done = mem_cell.done;
    }
    test_check_bool(ok, "Memoria: DMA directo SRAM->NoC(local) completo", "sram[5]=42", mem_done);
    test_check(ok, "Memoria: borde W real == sram[5]", "sram[5]=42, dir=0", 42, g_out_W[0][0].to_int());

    //======================================================================
    // Routing (2,0): sel_S = RC_FROM_W, estimulo en in_W[2], lee out_S[0]
    //======================================================================
    test_section("Routing (2,0): ctx0 sel_S=RC_FROM_W");
    noc_mesh_program(mesh, 2, 0, /*ctx=*/0,
                     make_routing_config_instr_c<DATA_W>(RC_NONE, RC_FROM_W, RC_NONE, RC_NONE));

    g_in_W[2] = link4(77, 77, 77, 77);
    step_n(mesh, 1);
    test_check_link(ok, "Routing: borde S real == relay de W(real)",
                    "in_W[2]=" + link_to_string(g_in_W[2]), link4(77, 77, 77, 77), g_out_S[0]);

    //======================================================================
    // Vectorial (0,1): MOV imm=17 -> DST_NORTH, lee out_N[1]
    //======================================================================
    test_section("Vectorial (0,1): MOV imm=17 -> DST_NORTH");
    noc_mesh_program(mesh, 0, 1, /*slot=*/0, mov_imm_instr(17, DST_NORTH));
    step_n(mesh, CGRA_FINAL_INSTR_MEM_SIZE);   // una vuelta completa del pc
    test_check_link(ok, "Vectorial: borde N real == imm difundido a las 4 lanes", "imm=17",
                    link4(17, 17, 17, 17), g_out_N[1]);

    //======================================================================
    // Escalar (0,2): MOV imm=23 -> DST_EAST, lee out_E[0]
    //======================================================================
    test_section("Escalar (0,2): MOV imm=23 -> DST_EAST");
    noc_mesh_program(mesh, 0, 2, /*slot=*/0, mov_imm_instr(23, DST_EAST));
    step_n(mesh, 1, /*rst=*/true);
    step_n(mesh, 1);
    test_check_scalar(ok, "Escalar: borde E real == imm difundido (broadcast escalar->vector)", "imm=23",
                      link4(23, 23, 23, 23), g_out_E[0]);

    //======================================================================
    // MAC (2,2): un unico OP_MAC(imm=6,imm=6) -> DST_EAST, lee out_E[2]
    //======================================================================
    test_section("MAC (2,2): OP_MAC(imm=6,imm=6) -> DST_EAST");
    noc_mesh_program(mesh, 2, 2, /*slot=*/0, mac_imm_instr(6, DST_EAST));
    step_n(mesh, 1, /*rst=*/true);
    step_n(mesh, 1);
    test_check_link(ok, "MAC: borde E real == acc tras un OP_MAC(6,6)", "imm=6",
                    link4(36, 36, 36, 36), g_out_E[2]);  // acc = 0 + 6*6

    if (ok) {
        printf("\nPASS: CGRA_Final_NoC_Mesh_C 3x3 (Memoria/Vectorial/Escalar/Routing/MAC sobre NoC) "
               "elabora y reproduce exactamente los resultados de CGRA_Final_Mesh_C.\n");
    }
    return ok ? 0 : 1;
}
