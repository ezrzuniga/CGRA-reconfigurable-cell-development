// CGRA_Mesh__TB.cpp
// Smoke test de una malla 2x2 de PE_scalar: valida el wiring entre celdas (no
// la carga de programas, ya cubierta por pe/scalar/PE_scalar__TB.cpp). Usa
// INSTR_MEM_SIZE=1 para que cada PE ejecute su unica instruccion en todos los
// ciclos, y programa una ruta en L que atraviesa 3 de las 4 PEs ejercitando
// ambos ejes de la malla:
//   pe(0,0): out_E = in_W            (borde oeste -> enlace interno E/W)
//   pe(0,1): out_S = in_W            (enlace interno E/W -> enlace interno N/S)
//   pe(1,1): out_E = in_N            (enlace interno N/S -> borde este)
// pe(1,0) queda sin programar (NOP) para probar que no interfiere.

#include <systemc.h>
#include "PE_scalar.h"
#include "CGRA_Mesh.h"

static const int ROWS = 2;
static const int COLS = 2;
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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_wave");
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

    // Programar la ruta en L: (0,0) -> (0,1) -> (1,1)
    PE::Instr mov_west_to_east;
    mov_west_to_east.opcode = OP_MOV;
    mov_west_to_east.src_a = SRC_WEST;
    mov_west_to_east.dst = DST_EAST;
    mesh.load_instr(0, 0, 0, mov_west_to_east);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 0);

    PE::Instr mov_west_to_south;
    mov_west_to_south.opcode = OP_MOV;
    mov_west_to_south.src_a = SRC_WEST;
    mov_west_to_south.dst = DST_SOUTH;
    mesh.load_instr(0, 1, 0, mov_west_to_south);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 1);

    PE::Instr mov_north_to_east;
    mov_north_to_east.opcode = OP_MOV;
    mov_north_to_east.src_a = SRC_NORTH;
    mov_north_to_east.dst = DST_EAST;
    mesh.load_instr(1, 1, 0, mov_north_to_east);
    sc_start(10, SC_NS);
    mesh.clear_instr(1, 1);

    // Inyectar el valor en el borde oeste, fila 0, y dejar que se propague
    // por los 3 saltos (1 ciclo por salto, dataflow sistolico).
    in_W[0].write(42);
    sc_start(60, SC_NS);

    cout << "out_E[1] = " << out_E[1].read() << endl;
    if (out_E[1].read() != 42) {
        cout << "FAIL: esperaba 42 en out_E[1], obtuve " << out_E[1].read() << endl;
        sc_close_vcd_trace_file(tf);
        return 1;
    }
    cout << "PASS: valor inyectado en in_W[0] llego a out_E[1] via (0,0)->(0,1)->(1,1)" << endl;

    sc_close_vcd_trace_file(tf);
    return 0;
}
