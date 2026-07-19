// CGRA_Mesh_Heterogeneous__TB.cpp
// Smoke test de una malla 1x3 mezclando celdas escalares y vectoriales: Vector -
// Scalar - Vector. Las 3 celdas corren la misma instruccion (out_E = in_W,
// INSTR_MEM_SIZE=1) y el test valida la convencion de conversion en cada borde de
// tipo:
//   pe(0,0) vector: pasa in_W tal cual (valida que el wire tipado PE_VectorData
//     funciona de punta a punta con lanes NO uniformes antes de cruzar ningun
//     limite escalar).
//   pe(0,1) escalar: consume la lane 0 del vector de (0,0) (bridge wire->escalar).
//   pe(0,2) vector: consume el broadcast del escalar de (0,1) (bridge
//     escalar->wire).
// Estimulo in_W[0] = {10,20,30,40} (deliberadamente no uniforme por lane) debe
// llegar a out_E[0] como {10,10,10,10}: si el bridge leyera otra lane que no sea la
// 0 el resultado seria {20,20,20,20}/{30,...}/{40,...}; si el broadcast rellenara
// con cero en vez de difundir seria {10,0,0,0}.

#include <systemc.h>
#include "CGRA_Mesh_Heterogeneous.h"

static const int ROWS = 1;
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

    std::vector<CellKind> layout = { CellKind::VECTOR, CellKind::SCALAR, CellKind::VECTOR };
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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_heterogeneous_wave");
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

    Mesh::Instr mov_west_to_east;
    mov_west_to_east.opcode = OP_MOV;
    mov_west_to_east.src_a = SRC_WEST;
    mov_west_to_east.dst = DST_EAST;

    mesh.load_instr(0, 0, 0, mov_west_to_east);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 0);

    mesh.load_instr(0, 1, 0, mov_west_to_east);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 1);

    mesh.load_instr(0, 2, 0, mov_west_to_east);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 2);

    // Inyectar un vector con lanes no uniformes en el borde oeste y dejar que se
    // propague por los 3 saltos (1 ciclo por salto, dataflow sistolico).
    in_W[0].write(Link({10, 20, 30, 40}));
    sc_start(60, SC_NS);

    Link expected({10, 10, 10, 10});
    Link got = out_E[0].read();
    cout << "out_E[0] = " << got << endl;
    if (got != expected) {
        cout << "FAIL: esperaba " << expected << " en out_E[0], obtuve " << got << endl;
        sc_close_vcd_trace_file(tf);
        return 1;
    }
    cout << "PASS: vector no uniforme inyectado en in_W[0] llego a out_E[0] como "
         << expected << " via (0,0 vector)->(0,1 escalar)->(0,2 vector)" << endl;

    sc_close_vcd_trace_file(tf);
    return 0;
}
