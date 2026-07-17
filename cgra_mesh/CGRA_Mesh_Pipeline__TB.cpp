// CGRA_Mesh_Pipeline__TB.cpp
// Testbench de una malla 3x3 donde la fila 0 actua como pipeline aritmetico
// de 3 etapas (cada PE ejecuta una unica instruccion fija, INSTR_MEM_SIZE=1,
// mismo criterio que CGRA_Mesh__TB.cpp). Las filas 1 y 2 quedan sin programar
// (NOP), fuera de alcance de este test:
//   pe(0,0): c = a + b   (a = in_W[0], b = in_N[0])
//   pe(0,1): e = c * d   (c = enlace interno desde (0,0), d = in_N[1])
//   pe(0,2): g = e & f   (e = enlace interno desde (0,1), f = in_N[2])
// El resultado final g sale por el borde este de la fila 0 (out_E[0]).

#include <systemc.h>
#include "PE_scalar.h"
#include "CGRA_Mesh.h"

static const int ROWS = 3;
static const int COLS = 3;
typedef PE_scalar<32, 8, 1> PE;
typedef CGRA_Mesh<PE, ROWS, COLS> Mesh;

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<sc_int<32>> in_N[COLS], out_N[COLS];
    sc_signal<sc_int<32>> in_S[COLS], out_S[COLS];
    sc_signal<sc_int<32>> in_W[ROWS], out_W[ROWS];
    sc_signal<sc_int<32>> in_E[ROWS], out_E[ROWS];

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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_pipeline_wave");
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
    for (int c = 0; c < COLS; c++) { in_N[c].write(0); in_S[c].write(0); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(0); in_E[r].write(0); }
    sc_start(20, SC_NS);

    rst.write(false);
    enable.write(true);

    // Programar el pipeline de la fila 0: (0,0) -> (0,1) -> (0,2)
    PE::Instr add_c;
    add_c.opcode = OP_ADD;
    add_c.src_a = SRC_WEST;
    add_c.src_b = SRC_NORTH;
    add_c.dst = DST_EAST;
    mesh.load_instr(0, 0, 0, add_c);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 0);

    PE::Instr mul_e;
    mul_e.opcode = OP_MUL;
    mul_e.src_a = SRC_WEST;
    mul_e.src_b = SRC_NORTH;
    mul_e.dst = DST_EAST;
    mesh.load_instr(0, 1, 0, mul_e);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 1);

    PE::Instr and_g;
    and_g.opcode = OP_AND;
    and_g.src_a = SRC_WEST;
    and_g.src_b = SRC_NORTH;
    and_g.dst = DST_EAST;
    mesh.load_instr(0, 2, 0, and_g);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 2);

    // Inyectar operandos: a=5, b=7 -> c=12; d=3 -> e=36; f=15 -> g=36&15=4.
    // Se sostienen constantes mientras el pipeline se propaga.
    in_W[0].write(5);
    in_N[0].write(7);
    in_N[1].write(3);
    in_N[2].write(15);
    sc_start(60, SC_NS);

    sc_int<32> g = out_E[0].read();
    cout << "out_E[0] = " << g << endl;
    if (g != 4) {
        cout << "FAIL: esperaba g=4 en out_E[0], obtuve " << g << endl;
        sc_close_vcd_trace_file(tf);
        return 1;
    }
    cout << "PASS: pipeline (0,0)->(0,1)->(0,2) c=a+b, e=c*d, g=e&f "
         << "produjo g=4 en out_E[0]" << endl;

    sc_close_vcd_trace_file(tf);
    return 0;
}
