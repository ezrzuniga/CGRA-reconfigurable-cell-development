// CGRA_Mesh_SmokeTest__TB.cpp
// Resumen rapido de las 3 variantes de PE en una sola malla minima: 3 filas x 1
// columna, una PE de cada tipo, cada una corriendo la operacion que mejor la
// representa:
//   fila 0 PE_scalar: c = a + b          (OP_ADD)
//   fila 1 PE_vector : e = a * k         (OP_MUL, k en broadcast via SRC_IMM)
//   fila 2 PE_MAC    : acc += a * k      (OP_MAC, acumula 1 ciclo por ciclo)
// Con COLS=1 cada fila tiene su in_W[r]/out_E[r] como borde REAL de la malla (ver
// CGRA_Mesh_Heterogeneous.h), asi que las 3 PEs se estimulan y se leen de forma
// independiente, sin celdas de relevo.

#include <systemc.h>
#include "CGRA_Mesh_Heterogeneous.h"

static const int ROWS = 3;
static const int COLS = 1;
typedef CGRA_Mesh_Heterogeneous<ROWS, COLS, 32, 4, 8, 1> Mesh;
typedef Mesh::Link Link;

static Link lane_delta(const Link& before, const Link& after) {
    Link d;
    for (int i = 0; i < 4; i++) d[i] = after[i] - before[i];
    return d;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

    // layout es plano y row-major (indice = r*COLS+c); su tamano debe calzar
    // exacto con ROWS*COLS (ver mesh/README.md, seccion "Definir un layout").
    std::vector<CellKind> layout = {
        CellKind::SCALAR,  // fila 0
        CellKind::VECTOR,  // fila 1
        CellKind::MAC,     // fila 2
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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_smoke_test_wave");
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

    // ---- Fila 0 (PE_scalar): c = a + b -------------------------------------
    // opcode=OP_ADD, src_a=SRC_WEST (a), src_b=SRC_NORTH (b), dst=DST_EAST.
    Mesh::Instr add_c;
    add_c.opcode = OP_ADD;
    add_c.src_a = SRC_WEST;
    add_c.src_b = SRC_NORTH;
    add_c.dst = DST_EAST;

    // ---- Fila 1 (PE_vector): e = a * k, k en broadcast a las 4 lanes ------
    // opcode=OP_MUL, src_a=SRC_WEST (a, vector), src_b=SRC_IMM (k escalar,
    // PE_vector lo difunde a las VLEN lanes), dst=DST_EAST.
    Mesh::Instr mul_e;
    mul_e.opcode = OP_MUL;
    mul_e.src_a = SRC_WEST;
    mul_e.src_b = SRC_IMM;
    mul_e.imm = 3;  // k
    mul_e.dst = DST_EAST;

    // ---- Fila 2 (PE_MAC): acc += a * k, un ciclo por acumulacion ----------
    // opcode=OP_MAC, src_a=SRC_WEST (a), src_b=SRC_IMM (k), dst=DST_EAST expone
    // el acumulador ya actualizado el mismo ciclo.
    Mesh::Instr mac_acc;
    mac_acc.opcode = OP_MAC;
    mac_acc.src_a = SRC_WEST;
    mac_acc.src_b = SRC_IMM;
    mac_acc.imm = 10;  // k
    mac_acc.dst = DST_EAST;

    // load_instr necesita un flanco de clk para latchear; addr=0 en las tres
    // porque esta malla usa INSTR_MEM_SIZE=1 (ver mesh/README.md).
    mesh.load_instr(0, 0, 0, add_c);
    mesh.load_instr(1, 0, 0, mul_e);
    mesh.load_instr(2, 0, 0, mac_acc);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 0);
    mesh.clear_instr(1, 0);
    mesh.clear_instr(2, 0);

    // Estimulo: a=5,b=7 -> c=12 (broadcast, celda escalar).
    in_W[0].write(Link({5, 5, 5, 5}));
    in_N[0].write(Link({7, 7, 7, 7}));

    // Estimulo: a={1,2,3,4} (lanes no uniformes), k=3 -> e={3,6,9,12}.
    in_W[1].write(Link({1, 2, 3, 4}));

    // Estimulo: a={2,3,4,5} constante, k=10 -> acc crece {20,30,40,50}/ciclo.
    in_W[2].write(Link({2, 3, 4, 5}));

    // Deja asentar la carga de instrucciones y el primer ciclo de computo.
    sc_start(20, SC_NS);

    bool ok = true;

    Link expected0({12, 12, 12, 12});
    Link got0 = out_E[0].read();
    cout << "out_E[0] = " << got0 << endl;
    if (got0 != expected0) {
        cout << "FAIL fila 0: esperaba " << expected0 << ", obtuve " << got0 << endl;
        ok = false;
    } else {
        cout << "PASS fila 0 (PE_scalar): c = a + b = " << got0 << endl;
    }

    Link expected1({3, 6, 9, 12});
    Link got1 = out_E[1].read();
    cout << "out_E[1] = " << got1 << endl;
    if (got1 != expected1) {
        cout << "FAIL fila 1: esperaba " << expected1 << ", obtuve " << got1 << endl;
        ok = false;
    } else {
        cout << "PASS fila 1 (PE_vector): e = a * k (broadcast) = " << got1 << endl;
    }

    // Fila 2: el acumulador sigue creciendo cada ciclo, se verifica por delta.
    Link snapshot1 = out_E[2].read();
    sc_start(10, SC_NS);
    Link snapshot2 = out_E[2].read();
    Link delta2 = lane_delta(snapshot1, snapshot2);
    Link expected_delta2({20, 30, 40, 50});
    cout << "delta acumulador fila 2 (1 ciclo) = " << delta2 << endl;
    if (delta2 != expected_delta2) {
        cout << "FAIL fila 2: esperaba delta " << expected_delta2 << ", obtuve " << delta2 << endl;
        ok = false;
    } else {
        cout << "PASS fila 2 (PE_MAC): acc += a * k crecio " << expected_delta2 << " en 1 ciclo" << endl;
    }

    sc_close_vcd_trace_file(tf);
    return ok ? 0 : 1;
}
