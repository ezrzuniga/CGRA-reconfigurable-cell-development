// CGRA_Final_NoC_Mesh__TB.cpp
// Mismo smoke test estructural que cgra_final/CGRA_Final_Mesh__TB.cpp (mismos
// estimulos, mismos valores esperados, mismos margenes de ciclos), aplicado a
// CGRA_Final_NoC_Mesh en vez de CGRA_Final_Mesh. El objetivo de reusar
// exactamente los mismos vectores es demostrar equivalencia funcional
// ciclo-a-ciclo entre la malla de wires directos y la malla NoC: si este
// archivo pasa con los mismos resultados que el original, la fabrica de
// routers (NoC_Router.h) esta reenviando cada paquete exactamente donde el
// wire directo lo hubiera llevado, en el mismo numero de ciclos.
//
//   Memoria (0,0): precarga con write_sram(), rafaga directa (ctx0, dir=0)
//                  y lectura por out_W[0] (borde real oeste).
//   Vectorial(0,1): MOV imm->DST_NORTH, lectura por out_N[1] (borde real norte).
//   Escalar (0,2):  MOV imm->DST_EAST, lectura por out_E[0] (borde real este).
//   Routing (2,0):  contexto sel_S=RC_FROM_W, estimulo en in_W[2], lectura
//                   por out_S[0] (celda con dos bordes reales).
//   MAC (2,2):      un unico OP_MAC(imm,imm)->DST_EAST, lectura por out_E[2].

#include <systemc.h>
#include <sstream>
#include "CGRA_Final_NoC_Mesh.h"
#include "../pe_hls/test_util.h"

static const int ROWS = CGRA_FINAL_NOC_ROWS;
static const int COLS = CGRA_FINAL_NOC_COLS;
typedef CGRA_Final_NoC_Mesh Mesh;
typedef CGRA_Final_NoC_Link Link;
typedef CGRA_Final_NoC_Instr Instr;

static Instr memory_field_instr(int field, int32_t value) {
    return make_memory_field_instr<CGRA_FINAL_NOC_DATA_W>(field, value);
}

static Instr mov_imm_instr(int32_t imm, sc_uint<3> dst) {
    Instr i;
    i.opcode = OP_MOV;
    i.src_a = SRC_IMM;
    i.imm = imm;
    i.dst = dst;
    return i;
}

static Instr mac_imm_instr(int32_t imm, sc_uint<3> dst) {
    Instr i;
    i.opcode = OP_MAC;
    i.src_a = SRC_IMM;
    i.src_b = SRC_IMM;
    i.imm = imm;
    i.dst = dst;
    return i;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

    Mesh mesh("mesh");
    mesh.clk(clk);
    mesh.rst(rst);
    mesh.enable(enable);
    for (int c = 0; c < COLS; c++) {
        mesh.in_N[c](in_N[c]);   mesh.out_N[c](out_N[c]);
        mesh.in_S[c](in_S[c]);   mesh.out_S[c](out_S[c]);
    }
    for (int r = 0; r < ROWS; r++) {
        mesh.in_W[r](in_W[r]);   mesh.out_W[r](out_W[r]);
        mesh.in_E[r](in_E[r]);   mesh.out_E[r](out_E[r]);
    }

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    advance_cycles(2);
    rst.write(false);
    enable.write(true);
    cout << "layout 3x3 (NoC): (0,0)=Memoria (0,1)=Vectorial (0,2)=Escalar / "
         << "(1,0)=Routing (1,1)=MAC (1,2)=MAC / (2,0)=Routing (2,1)=MAC (2,2)=MAC" << endl;

    bool ok = true;

    // Acceso tipado directo (indice row-major), sin static_cast.
    auto& mem_cell = mesh.cell<0>();  // (0,0)

    //======================================================================
    // Memoria (0,0): precarga sram[5]=42, rafaga directa SRAM->NoC(local), lee out_W[0]
    //======================================================================
    test_section("Memoria (0,0): precarga sram[5]=42 y rafaga directa (ctx0, dir=0)");
    mem_cell.write_sram(5, 42);
    mesh.load_instr(0, 0, 0, memory_field_instr(MEM_FIELD_SRC_ADDR, 5));
    advance_cycles(1);
    mesh.load_instr(0, 0, 0, memory_field_instr(MEM_FIELD_DST_ADDR, 0));
    advance_cycles(1);
    mesh.load_instr(0, 0, 0, memory_field_instr(MEM_FIELD_MODE, 0));
    advance_cycles(1);
    mesh.load_instr(0, 0, 0, memory_field_instr(MEM_FIELD_DIR, 0));
    advance_cycles(1);
    mesh.load_instr(0, 0, 0, memory_field_instr(MEM_FIELD_COUNT, 1));
    advance_cycles(1);
    mesh.load_instr(0, 0, 0, memory_field_instr(MEM_FIELD_START, 1));
    advance_cycles(1);
    mesh.clear_instr(0, 0);

    bool mem_done = false;
    for (int i = 0; i < 10 && !mem_done; i++) {
        advance_cycles(1);
        mem_done = mem_cell.dma_done();
    }
    test_check_bool(ok, "Memoria: DMA directo SRAM->NoC(local) completo", "sram[5]=42", mem_done);
    test_check(ok, "Memoria: borde W real == sram[5]", "sram[5]=42, dir=0",
               sc_int<32>(42), out_W[0].read()[0]);

    //======================================================================
    // Routing (2,0): sel_S = RC_FROM_W, estimulo en in_W[2], lee out_S[0]
    //======================================================================
    test_section("Routing (2,0): ctx0 sel_S=RC_FROM_W");
    mesh.load_instr(2, 0, 0, make_routing_config_instr_hls<CGRA_FINAL_NOC_DATA_W>(RC_NONE, RC_FROM_W, RC_NONE, RC_NONE));
    advance_cycles(1);
    mesh.clear_instr(2, 0);

    in_W[2].write(Link({77, 77, 77, 77}));
    advance_cycles(2);
    {
        Link expected({77, 77, 77, 77});
        std::ostringstream in;
        in << "in_W[2](Routing)=" << in_W[2].read();
        test_check(ok, "Routing: borde S real == relay de W(real)", in.str(), expected, out_S[0].read());
    }

    //======================================================================
    // Vectorial (0,1): MOV imm=17 -> DST_NORTH, lee out_N[1]
    //======================================================================
    test_section("Vectorial (0,1): MOV imm=17 -> DST_NORTH");
    mesh.load_instr(0, 1, 0, mov_imm_instr(17, DST_NORTH));
    advance_cycles(1);
    mesh.clear_instr(0, 1);

    // Mismo margen (una vuelta completa de INSTR_MEM_SIZE) que el original
    // -- ver cgra_final/CGRA_Final_Mesh__TB.cpp para la explicacion completa.
    advance_cycles(CGRA_FINAL_NOC_INSTR_MEM_SIZE);
    {
        Link expected({17, 17, 17, 17});
        test_check(ok, "Vectorial: borde N real == imm difundido a las 4 lanes", "imm=17", expected, out_N[1].read());
    }

    //======================================================================
    // Escalar (0,2): MOV imm=23 -> DST_EAST, lee out_E[0]
    //======================================================================
    test_section("Escalar (0,2): MOV imm=23 -> DST_EAST");
    mesh.load_instr(0, 2, 0, mov_imm_instr(23, DST_EAST));
    advance_cycles(1);
    mesh.clear_instr(0, 2);

    rst.write(true); advance_cycles(1); rst.write(false);
    advance_cycles(1);
    {
        Link expected({23, 23, 23, 23});
        test_check(ok, "Escalar: borde E real == imm difundido (bridge escalar->vector)", "imm=23", expected, out_E[0].read());
    }

    //======================================================================
    // MAC (2,2): un unico OP_MAC(imm=6,imm=6) -> DST_EAST, lee out_E[2]
    //======================================================================
    test_section("MAC (2,2): OP_MAC(imm=6,imm=6) -> DST_EAST");
    mesh.load_instr(2, 2, 0, mac_imm_instr(6, DST_EAST));
    advance_cycles(1);
    mesh.clear_instr(2, 2);

    advance_cycles(CGRA_FINAL_NOC_INSTR_MEM_SIZE);
    {
        Link expected({36, 36, 36, 36});  // acc = 0 + 6*6, escrito directo a DST_EAST por el propio OP_MAC
        test_check(ok, "MAC: borde E real == acc tras un OP_MAC(6,6)", "imm=6", expected, out_E[2].read());
    }

    if (ok) {
        cout << "\nPASS: CGRA_Final_NoC_Mesh 3x3 (Memoria/Vectorial/Escalar/Routing/MAC sobre NoC) "
             << "elabora y reproduce exactamente los resultados de CGRA_Final_Mesh." << endl;
    }
    return ok ? 0 : 1;
}
