// CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp
// Malla 2x2 con las 4 celdas heterogeneas del diagrama de nivel 2
// (Entrega_Avance_2/images/lvl2_diagram.png): Enrutamiento, Memoria, Escalar,
// Vectorial, en ese layout row-major:
//   (0,0) Enrutamiento   (0,1) Memoria
//   (1,0) Escalar        (1,1) Vectorial
// que reproduce exactamente la adyacencia del diagrama: Enrutamiento-Memoria y
// Escalar-Vectorial horizontales, Enrutamiento-Escalar y Memoria-Vectorial
// verticales -- son enlaces INTERNOS de malla (sig_horiz_*/sig_vert_* de
// CGRA_Mesh_Heterogeneous), no bordes externos.
//
// Bordes reales de esta malla 2x2 (unica combinacion valida por posicion):
//   Enrutamiento (0,0): N, W       Memoria (0,1): N, E
//   Escalar      (1,0): S, W      Vectorial (1,1): S, E
//
// Dos escenarios independientes:
//  1) Enrutamiento <-> Memoria sobre el enlace interno: el borde W real de
//     Enrutamiento se rutea (crossbar, ctx0) hacia Memoria, que lo dma-copia a su
//     SRAM (NoC->SRAM); luego Memoria lo dma-copia de vuelta (SRAM->NoC) y
//     Enrutamiento (ctx1) lo relay-ea de vuelta al borde W real -- round trip
//     completo Routing->Memory->Routing que ejercita el puente TLM<->Link de
//     PE_Memory_Mesh_Cell (ver memory/PE_Memory_Mesh_Cell.h, "Simplificaciones
//     conscientes": el puerto NoC de la celda de Memoria solo esta atado al borde
//     oeste, que es exactamente el que la conecta con Enrutamiento en este layout).
//  2) Escalar <-> Vectorial sobre el enlace interno: c = a(W real) + b(S real) en
//     Escalar, difundido hacia Vectorial (enlace interno), que hace e = c * k y
//     saca el resultado por su borde E real.
#include <systemc.h>
#include <sstream>
#include "CGRA_Mesh_Heterogeneous.h"
#include "../pe/test_util.h"

static const int ROWS = 2;
static const int COLS = 2;
typedef CGRA_Mesh_Heterogeneous<ROWS, COLS, 32, 4, 8, 1> Mesh;
typedef Mesh::Link Link;
typedef Mesh::Instr Instr;

// Helpers de empaquetado de instrucciones para Enrutamiento/Memoria: ver
// make_routing_config_instr (pe/routing/PE_Routing_Cell.h) y make_memory_field_instr
// (memory/PE_Memory_Mesh_Cell.h) -- compartidos con mesh_wrapper.cpp para no
// reimplementar el mismo empaquetado dos veces.
static Instr routing_instr(sc_uint<4> sel_N, sc_uint<4> sel_S, sc_uint<4> sel_E, sc_uint<4> sel_W) {
    return make_routing_config_instr<32>(sel_N, sel_S, sel_E, sel_W);
}

static Instr memory_field_instr(int field, int32_t value) {
    return make_memory_field_instr<32>(field, value);
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

    std::vector<CellKind> layout = {
        CellKind::ROUTING, CellKind::MEMORY,   // fila 0: (0,0) (0,1)
        CellKind::SCALAR,  CellKind::VECTOR,   // fila 1: (1,0) (1,1)
    };
    Mesh mesh("mesh", layout);
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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_2x2_heterogeneous_test_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, enable, "enable");
    mesh.trace(tf);

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    advance_cycles(2);
    rst.write(false);
    enable.write(true);
    cout << "layout 2x2: (0,0)=Enrutamiento (0,1)=Memoria (1,0)=Escalar (1,1)=Vectorial" << endl;

    bool ok = true;

    // Acceso directo a las celdas concretas para inspeccionar estado interno
    // (SRAM, status del DMA local) que no es visible por los puertos de malla.
    auto& mem_cell = static_cast<PE_Memory_Mesh_Cell<32, 4>&>(mesh.pe[1]);  // (0,1)

    //======================================================================
    // Escenario 1: Enrutamiento <-> Memoria (enlace interno E/W fila 0)
    //======================================================================
    test_section("Enrutamiento ctx0: W(real) -> E(interno, hacia Memoria)");
    mesh.load_instr(0, 0, /*ctx*/ 0, routing_instr(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE));
    advance_cycles(1);
    mesh.clear_instr(0, 0);

    test_section("Memoria ctx0: NoC->SRAM (dir=1), copia el word entrante a sram[0]");
    mesh.load_instr(0, 1, 0, memory_field_instr(MEM_FIELD_DIR, 1));
    advance_cycles(1);
    mesh.load_instr(0, 1, 0, memory_field_instr(MEM_FIELD_MODE, 0));
    advance_cycles(1);
    mesh.load_instr(0, 1, 0, memory_field_instr(MEM_FIELD_COUNT, 1));
    advance_cycles(1);
    mesh.load_instr(0, 1, 0, memory_field_instr(MEM_FIELD_SRC_ADDR, 0));
    advance_cycles(1);
    mesh.load_instr(0, 1, 0, memory_field_instr(MEM_FIELD_DST_ADDR, 0));
    advance_cycles(1);
    mesh.clear_instr(0, 1);

    test_section("Estimulo: borde W real de Enrutamiento = 100 (lane 0)");
    in_W[0].write(Link({100, 0, 0, 0}));
    advance_cycles(2);

    test_section("Memoria ctx0: START");
    mesh.load_instr(0, 1, 0, memory_field_instr(MEM_FIELD_START, 1));
    advance_cycles(1);
    mesh.clear_instr(0, 1);

    bool dma1_done = false;
    for (int i = 0; i < 10 && !dma1_done; i++) {
        advance_cycles(1);
        dma1_done = mem_cell.dma_done();
    }
    {
        std::ostringstream in;
        in << "in_W[0](Enrutamiento)=" << in_W[0].read();
        test_check_bool(ok, "Memoria: DMA NoC->SRAM completo", in.str(), dma1_done);
    }
    {
        uint32_t got = mem_cell.inner.sram.read_word(0);
        std::ostringstream in;
        in << "in_W[0](Enrutamiento)=" << in_W[0].read();
        test_check(ok, "Memoria: sram[0] == 100 (via Enrutamiento ctx0)", in.str(),
                   (uint32_t)100, got);
    }

    test_section("Memoria ctx1: SRAM->NoC (dir=0), reenvia sram[0] hacia Enrutamiento");
    mesh.load_instr(0, 1, 1, memory_field_instr(MEM_FIELD_DIR, 0));
    advance_cycles(1);
    mesh.load_instr(0, 1, 1, memory_field_instr(MEM_FIELD_MODE, 0));
    advance_cycles(1);
    mesh.load_instr(0, 1, 1, memory_field_instr(MEM_FIELD_COUNT, 1));
    advance_cycles(1);
    mesh.load_instr(0, 1, 1, memory_field_instr(MEM_FIELD_SRC_ADDR, 0));
    advance_cycles(1);
    mesh.load_instr(0, 1, 1, memory_field_instr(MEM_FIELD_DST_ADDR, 0));
    advance_cycles(1);
    mesh.load_instr(0, 1, 1, memory_field_instr(MEM_FIELD_START, 1));
    advance_cycles(1);
    mesh.clear_instr(0, 1);

    bool dma2_done = false;
    for (int i = 0; i < 10 && !dma2_done; i++) {
        advance_cycles(1);
        dma2_done = mem_cell.dma_done();
    }
    test_check_bool(ok, "Memoria: DMA SRAM->NoC completo", "sram[0]=100", dma2_done);

    test_section("Enrutamiento ctx1: E(interno, desde Memoria) -> W(real)");
    mesh.load_instr(0, 0, /*ctx*/ 1, routing_instr(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E));
    advance_cycles(2);
    mesh.clear_instr(0, 0);

    {
        Link expected({100, 0, 0, 0});
        std::ostringstream in;
        in << "sram[0]=100 reenviado por Memoria, Enrutamiento ctx1 relay E->W";
        test_check(ok, "Enrutamiento: borde W real == round trip por Memoria", in.str(),
                   expected, out_W[0].read());
    }

    //======================================================================
    // Escenario 2: Escalar <-> Vectorial (enlace interno E/W fila 1)
    //======================================================================
    test_section("Escalar: c = a(W real) + b(S real) -> E (interno, hacia Vectorial)");
    Instr add_c;
    add_c.opcode = OP_ADD;
    add_c.src_a = SRC_WEST;
    add_c.src_b = SRC_SOUTH;
    add_c.dst = DST_EAST;
    mesh.load_instr(1, 0, 0, add_c);
    advance_cycles(1);
    mesh.clear_instr(1, 0);

    test_section("Vectorial: e = a(W interno, desde Escalar) * k -> E (real)");
    Instr mul_e;
    mul_e.opcode = OP_MUL;
    mul_e.src_a = SRC_WEST;
    mul_e.src_b = SRC_IMM;
    mul_e.imm = 3;
    mul_e.dst = DST_EAST;
    mesh.load_instr(1, 1, 0, mul_e);
    advance_cycles(1);
    mesh.clear_instr(1, 1);

    test_section("Estimulo: borde W real de Escalar=5, borde S real de Escalar=7");
    in_W[1].write(Link({5, 5, 5, 5}));
    in_S[0].write(Link({7, 7, 7, 7}));
    advance_cycles(4);

    {
        Link expected({36, 36, 36, 36});  // (5+7)*3, difundido por Escalar a las 4 lanes
        std::ostringstream in;
        in << "in_W[1](Escalar,a)=" << in_W[1].read() << " in_S[0](Escalar,b)=" << in_S[0].read()
           << " k=" << mul_e.imm;
        test_check(ok, "Vectorial: borde E real == (a+b)*k via Escalar", in.str(), expected,
                   out_E[1].read());
    }

    sc_close_vcd_trace_file(tf);
    if (ok) {
        cout << "\nPASS: malla 2x2 heterogenea (Enrutamiento/Memoria/Escalar/Vectorial) "
             << "reproduce el interconnect del diagrama de nivel 2." << endl;
    }
    return ok ? 0 : 1;
}
