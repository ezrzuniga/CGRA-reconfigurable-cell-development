// CGRA_Mesh_1x1_Test__TB.cpp
// Malla minima posible: 1x1, una sola PE_vector, programada con una unica
// instruccion de suma (c = a + b). Con ROWS=COLS=1 la unica celda es borde
// oeste/este/norte/sur a la vez, asi que in_W/in_N/out_E son todos bordes
// reales de la malla -- se estimula in_W (a) e in_N (b) y se lee out_E (c).

#include <systemc.h>
#include <sstream>
#include "CGRA_Mesh_Heterogeneous.h"
#include "../pe/test_util.h"

static const int ROWS = 1;
static const int COLS = 1;
typedef CGRA_Mesh_Heterogeneous<ROWS, COLS, 32, 4, 8, 1> Mesh;
typedef Mesh::Link Link;

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

    // LAYOUT: 1x1, unica celda es PE_vector.
    std::vector<CellKind> layout = { CellKind::VECTOR };
    Mesh mesh("mesh", layout);
    mesh.clk(clk);
    mesh.rst(rst);
    mesh.enable(enable);
    mesh.in_N[0](in_N[0]);   mesh.out_N[0](out_N[0]);
    mesh.in_S[0](in_S[0]);   mesh.out_S[0](out_S[0]);
    mesh.in_W[0](in_W[0]);   mesh.out_W[0](out_W[0]);
    mesh.in_E[0](in_E[0]);   mesh.out_E[0](out_E[0]);

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_1x1_test_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, enable, "enable");
    sc_trace(tf, in_N[0],  "in_N_0");
    sc_trace(tf, out_N[0], "out_N_0");
    sc_trace(tf, in_S[0],  "in_S_0");
    sc_trace(tf, out_S[0], "out_S_0");
    sc_trace(tf, in_W[0],  "in_W_0");
    sc_trace(tf, out_W[0], "out_W_0");
    sc_trace(tf, in_E[0],  "in_E_0");
    sc_trace(tf, out_E[0], "out_E_0");
    mesh.trace(tf);

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    in_N[0].write(Link());
    in_S[0].write(Link());
    in_W[0].write(Link());
    in_E[0].write(Link());
    advance_cycles(2);
    cout << "rst=1 enable=0 durante 2 ciclos, entradas en 0" << endl;

    rst.write(false);
    enable.write(true);

    // Programar la instruccion de suma en la unica celda (0,0) de la malla.
    test_section("Carga de instruccion");
    // c = a + b: opcode=OP_ADD, src_a=SRC_WEST (a), src_b=SRC_NORTH (b),
    // dst=DST_EAST.
    Mesh::Instr add_c;
    add_c.opcode = OP_ADD;
    add_c.src_a = SRC_WEST;
    add_c.src_b = SRC_NORTH;
    add_c.dst = DST_EAST;

    mesh.load_instr(0, 0, 0, add_c);
    advance_cycles(1);
    mesh.clear_instr(0, 0);
    cout << "pe(0,0): ADD(a,b)->E" << endl;

    bool ok = true;

    // Estimulo:
    test_section("Estimulo");
    // a={1,2,3,4}, b={10,20,30,40} -> c={11,22,33,44}.
    in_W[0].write(Link({1, 2, 3, 4}));
    in_N[0].write(Link({10, 20, 30, 40}));
    advance_cycles(2);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read() << " in_N[0](b)=" << in_N[0].read();
        Link expected({11, 22, 33, 44});
        test_check(ok, "pe(0,0): c = a + b", in.str(), expected, out_E[0].read());
    }

    sc_close_vcd_trace_file(tf);
    return ok ? 0 : 1;
}
