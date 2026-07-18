// CGRA_Mesh_Pipeline_Heterogeneous__TB.cpp
// Malla 3x3 heterogenea: fila 0 reproduce EXACTAMENTE el pipeline escalar de
// CGRA_Mesh_Pipeline__TB.cpp (misma ISA, mismos operandos, mismo resultado g=4),
// fila 1 agrega un segundo pipeline independiente con celdas vectoriales:
//   pe(0,0) escalar: c = a + b   (a = in_W[0], b = in_N[0])
//   pe(0,1) escalar: e = c * d   (d = in_N[1])
//   pe(0,2) escalar: g = e & f   (f = in_N[2])            -> out_E[0] = broadcast(4)
//
//   pe(1,0) vectorial: h = a * k1   (a = in_W[1], k1 = SRC_IMM)
//   pe(1,1) vectorial: j = h + k2   (k2 = SRC_IMM)
//   pe(1,2) vectorial: m = j & k3   (k3 = SRC_IMM, mascara) -> out_E[1] = {13,0,3,6}
// La fila 1 no puede usar SRC_NORTH como la fila 0 (in_N/in_S solo son borde real en
// la primera/ultima fila de la malla; en la fila 1 son enlaces internos hacia las
// filas 0 y 2, que acoplarian los dos pipelines) — por eso el segundo operando de
// cada etapa es un inmediato (SRC_IMM), la unica logica genuinamente vectorial que
// tiene PE_vector (select_src difunde el inmediato escalar a las 4 lanes).
// Fila 2 queda sin programar (NOP), fuera de alcance.

#include <systemc.h>
#include "CGRA_Mesh_Heterogeneous.h"

static const int ROWS = 3;
static const int COLS = 3;
typedef CGRA_Mesh_Heterogeneous<ROWS, COLS, 32, 4, 8, 1> Mesh;
typedef Mesh::Link Link;

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

    std::vector<CellKind> layout = {
        CellKind::SCALAR, CellKind::SCALAR, CellKind::SCALAR,  // fila 0
        CellKind::VECTOR, CellKind::VECTOR, CellKind::VECTOR,  // fila 1
        CellKind::SCALAR, CellKind::SCALAR, CellKind::SCALAR,  // fila 2 (sin programar)
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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_pipeline_heterogeneous_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, enable, "enable");
    for (int c = 0; c < COLS; c++) {
        sc_trace(tf, in_N[c],  "in_N_" + std::to_string(c));
        sc_trace(tf, out_N[c], "out_N_" + std::to_string(c));
        sc_trace(tf, in_S[c],  "in_S_" + std::to_string(c));
        sc_trace(tf, out_S[c], "out_S_" + std::to_string(c));
    }
    for (int r = 0; r < ROWS; r++) {
        sc_trace(tf, in_W[r],  "in_W_" + std::to_string(r));
        sc_trace(tf, out_W[r], "out_W_" + std::to_string(r));
        sc_trace(tf, in_E[r],  "in_E_" + std::to_string(r));
        sc_trace(tf, out_E[r], "out_E_" + std::to_string(r));
    }
    mesh.trace(tf);

    // Reset
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    sc_start(20, SC_NS);

    rst.write(false);
    enable.write(true);

    // ---- Fila 0: mismo pipeline escalar que CGRA_Mesh_Pipeline__TB.cpp -------
    Mesh::Instr add_c;
    add_c.opcode = OP_ADD;
    add_c.src_a = SRC_WEST;
    add_c.src_b = SRC_NORTH;
    add_c.dst = DST_EAST;
    mesh.load_instr(0, 0, 0, add_c);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 0);

    Mesh::Instr mul_e;
    mul_e.opcode = OP_MUL;
    mul_e.src_a = SRC_WEST;
    mul_e.src_b = SRC_NORTH;
    mul_e.dst = DST_EAST;
    mesh.load_instr(0, 1, 0, mul_e);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 1);

    Mesh::Instr and_g;
    and_g.opcode = OP_AND;
    and_g.src_a = SRC_WEST;
    and_g.src_b = SRC_NORTH;
    and_g.dst = DST_EAST;
    mesh.load_instr(0, 2, 0, and_g);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 2);

    // ---- Fila 1: pipeline vectorial nuevo, independiente -----------------
    Mesh::Instr mul_h;
    mul_h.opcode = OP_MUL;
    mul_h.src_a = SRC_WEST;
    mul_h.src_b = SRC_IMM;
    mul_h.imm = 3;   // k1
    mul_h.dst = DST_EAST;
    mesh.load_instr(1, 0, 0, mul_h);
    sc_start(10, SC_NS);
    mesh.clear_instr(1, 0);

    Mesh::Instr add_j;
    add_j.opcode = OP_ADD;
    add_j.src_a = SRC_WEST;
    add_j.src_b = SRC_IMM;
    add_j.imm = 10;  // k2
    add_j.dst = DST_EAST;
    mesh.load_instr(1, 1, 0, add_j);
    sc_start(10, SC_NS);
    mesh.clear_instr(1, 1);

    Mesh::Instr and_m;
    and_m.opcode = OP_AND;
    and_m.src_a = SRC_WEST;
    and_m.src_b = SRC_IMM;
    and_m.imm = 15;  // k3 (mascara 0xF)
    and_m.dst = DST_EAST;
    mesh.load_instr(1, 2, 0, and_m);
    sc_start(10, SC_NS);
    mesh.clear_instr(1, 2);

    // Estimulo fila 0: a=5,b=7 -> c=12; d=3 -> e=36; f=15 -> g=36&15=4.
    // Escalar, difundido a las 4 lanes ya que alimenta celdas escalares.
    in_W[0].write(Link({5, 5, 5, 5}));
    in_N[0].write(Link({7, 7, 7, 7}));
    in_N[1].write(Link({3, 3, 3, 3}));
    in_N[2].write(Link({15, 15, 15, 15}));

    // Estimulo fila 1: a={1,2,3,4} (lanes no uniformes a proposito) ->
    // h=a*3={3,6,9,12} -> j=h+10={13,16,19,22} -> m=j&15={13,0,3,6}.
    in_W[1].write(Link({1, 2, 3, 4}));

    sc_start(60, SC_NS);

    bool ok = true;

    Link expected0({4, 4, 4, 4});
    Link got0 = out_E[0].read();
    cout << "out_E[0] = " << got0 << endl;
    if (got0 != expected0) {
        cout << "FAIL fila 0: esperaba " << expected0 << ", obtuve " << got0 << endl;
        ok = false;
    } else {
        cout << "PASS fila 0: pipeline escalar c=a+b, e=c*d, g=e&f produjo g=4 (broadcast) en out_E[0]" << endl;
    }

    Link expected1({13, 0, 3, 6});
    Link got1 = out_E[1].read();
    cout << "out_E[1] = " << got1 << endl;
    if (got1 != expected1) {
        cout << "FAIL fila 1: esperaba " << expected1 << ", obtuve " << got1 << endl;
        ok = false;
    } else {
        cout << "PASS fila 1: pipeline vectorial h=a*k1, j=h+k2, m=j&k3 produjo "
             << expected1 << " en out_E[1]" << endl;
    }

    sc_close_vcd_trace_file(tf);
    return ok ? 0 : 1;
}
