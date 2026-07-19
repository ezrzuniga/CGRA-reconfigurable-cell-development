// CGRA_Mesh_MAC_Heterogeneous__TB.cpp
// Malla 3x3 heterogenea con el tercer tipo de celda, PE_MAC: fila 0 reproduce
// el pipeline escalar de CGRA_Mesh_Pipeline_Heterogeneous__TB.cpp (regresion
// de wiring), filas 1 y 2 corren cada una un acumulador MAC vectorial real,
// pero con UNA sola instruccion (OP_MAC) en vez de las 3 de
// CGRA_Mesh_Pipeline_Heterogeneous__TB.cpp -- el delta se mide por CICLO, no
// por rotacion de 3 ciclos, y es el contraste directo con ese test (no se
// toca, sigue siendo el contrato congelado del MAC emulado).
//
//   pe(1,0)/pe(2,0) PE_MAC: acc += a * k, expuesto a DST_EAST el mismo ciclo
//   (in_W[r] constante durante toda la corrida). pe(r,1)/pe(r,2) relevan
//   (OP_MOV SRC_WEST->DST_EAST) para que el acumulador llegue al borde este
//   observable -- misma razon topologica que en el test de 3 ciclos: pe(r,0)
//   es columna oeste, su DST_EAST cae en un enlace interno.
//
// Ademas de la tasa de 1 ciclo, se ejercita SRC_ACC/DST_ACC en pe(1,0): se
// limpia su acumulador (OP_MOV imm=0 dst=DST_ACC) y se lee de vuelta
// (OP_MOV src_a=SRC_ACC dst=DST_EAST) sin resetear el resto de la malla,
// verificando en paralelo que pe(2,0) (fila 2) sigue acumulando sin
// interrupcion -- la prueba concreta de que el reset es por-PE, no global
// como rst (ver pe/CLAUDE.md).

#include <systemc.h>
#include "CGRA_Mesh_Heterogeneous.h"

static const int ROWS = 3;
static const int COLS = 3;
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

    std::vector<CellKind> layout = {
        CellKind::SCALAR, CellKind::SCALAR, CellKind::SCALAR,  // fila 0
        CellKind::MAC,     CellKind::MAC,     CellKind::MAC,    // fila 1
        CellKind::MAC,     CellKind::MAC,     CellKind::MAC,    // fila 2
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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_mac_heterogeneous_wave");
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

    // ---- Fila 0: mismo pipeline escalar de regresion ----------------------
    Mesh::Instr add_c;
    add_c.opcode = OP_ADD; add_c.src_a = SRC_WEST; add_c.src_b = SRC_NORTH; add_c.dst = DST_EAST;
    Mesh::Instr mul_e;
    mul_e.opcode = OP_MUL; mul_e.src_a = SRC_WEST; mul_e.src_b = SRC_NORTH; mul_e.dst = DST_EAST;
    Mesh::Instr and_g;
    and_g.opcode = OP_AND; and_g.src_a = SRC_WEST; and_g.src_b = SRC_NORTH; and_g.dst = DST_EAST;

    // ---- Filas 1 y 2: acumuladores MAC de 1 ciclo, con relevos -----------
    Mesh::Instr relay;
    relay.opcode = OP_MOV; relay.src_a = SRC_WEST; relay.dst = DST_EAST;

    Mesh::Instr mac1;
    mac1.opcode = OP_MAC; mac1.src_a = SRC_WEST; mac1.src_b = SRC_IMM; mac1.imm = 5; mac1.dst = DST_EAST;
    Link a1({1, 2, 3, 4});
    Link a1k1({5, 10, 15, 20});

    Mesh::Instr mac2;
    mac2.opcode = OP_MAC; mac2.src_a = SRC_WEST; mac2.src_b = SRC_IMM; mac2.imm = 2; mac2.dst = DST_EAST;
    Link a2({1, 1, 1, 1});
    Link a2k2({2, 2, 2, 2});

    mesh.load_instr(0, 0, 0, add_c);
    mesh.load_instr(0, 1, 0, mul_e);
    mesh.load_instr(0, 2, 0, and_g);
    mesh.load_instr(1, 0, 0, mac1);
    mesh.load_instr(1, 1, 0, relay);
    mesh.load_instr(1, 2, 0, relay);
    mesh.load_instr(2, 0, 0, mac2);
    mesh.load_instr(2, 1, 0, relay);
    mesh.load_instr(2, 2, 0, relay);
    sc_start(10, SC_NS);
    mesh.clear_instr(0, 0); mesh.clear_instr(0, 1); mesh.clear_instr(0, 2);
    mesh.clear_instr(1, 0); mesh.clear_instr(1, 1); mesh.clear_instr(1, 2);
    mesh.clear_instr(2, 0); mesh.clear_instr(2, 1); mesh.clear_instr(2, 2);

    // Estimulo fila 0: a=5,b=7 -> c=12; d=3 -> e=36; f=15 -> g=36&15=4.
    in_W[0].write(Link({5, 5, 5, 5}));
    in_N[0].write(Link({7, 7, 7, 7}));
    in_N[1].write(Link({3, 3, 3, 3}));
    in_N[2].write(Link({15, 15, 15, 15}));

    // Estimulo filas 1 y 2: constante durante toda la corrida.
    in_W[1].write(a1);
    in_W[2].write(a2);

    // Asienta fila 0 y la propagacion inicial de los relevos (2 saltos hasta
    // el borde este de las filas 1/2).
    sc_start(50, SC_NS);

    bool ok = true;

    Link expected0({4, 4, 4, 4});
    Link got0 = out_E[0].read();
    if (got0 != expected0) {
        cout << "FAIL fila 0: esperaba " << expected0 << ", obtuve " << got0 << endl;
        ok = false;
    } else {
        cout << "PASS fila 0: pipeline escalar produjo g=4 (broadcast) en out_E[0]" << endl;
    }

    // ---- Fila 1: delta de 1 ciclo (no 3) -----------------------------------
    Link snap1_before = out_E[1].read();
    sc_start(10, SC_NS);
    Link snap1_after = out_E[1].read();
    Link delta1 = lane_delta(snap1_before, snap1_after);
    if (delta1 != a1k1) {
        cout << "FAIL fila 1: esperaba delta " << a1k1 << " en 1 ciclo, obtuve " << delta1 << endl;
        ok = false;
    } else {
        cout << "PASS fila 1: PE_MAC acumulo " << a1k1 << " en 1 solo ciclo (vs 3 en el MAC emulado)" << endl;
    }

    // ---- SRC_ACC/DST_ACC en pe(1,0), fila 2 debe seguir sin interrupcion --
    // load_cell() da 3 ciclos de margen (no 2 como en el TB standalone de
    // pe/mac/): mesh.load_instr() escribe la senal instr_sig de la malla, que
    // PE_MAC_Cell::bridge_instr_in() (un SC_METHOD async adicional) recien
    // traduce hacia el instr_in interno de PE_MAC -- un salto de mas
    // respecto al TB standalone (que bindea instr_in directo), asi que hace
    // falta un ciclo extra de margen para garantizar que instr_mem ya este
    // estable.
    Link row2_before = out_E[2].read();
    int cycles_elapsed = 0;

    auto load_cell = [&](int row, int col, const Mesh::Instr& instr) {
        mesh.load_instr(row, col, 0, instr);
        sc_start(10, SC_NS);
        mesh.clear_instr(row, col);
        sc_start(20, SC_NS);
        cycles_elapsed += 3;
    };

    Mesh::Instr clear_acc;
    clear_acc.opcode = OP_MOV; clear_acc.src_a = SRC_IMM; clear_acc.imm = 0; clear_acc.dst = DST_ACC;
    load_cell(1, 0, clear_acc);

    Mesh::Instr read_acc;
    read_acc.opcode = OP_MOV; read_acc.src_a = SRC_ACC; read_acc.dst = DST_EAST;
    load_cell(1, 0, read_acc);

    // 3 ciclos mas para que el 0 leido de vuelta propague por los relevos
    // pe(1,1)->pe(1,2) hasta el borde este observable.
    sc_start(30, SC_NS);
    cycles_elapsed += 3;

    Link acc_after_clear = out_E[1].read();
    if (acc_after_clear != Link()) {
        cout << "FAIL: DST_ACC no limpio el acumulador de pe(1,0), out_E[1] = " << acc_after_clear << endl;
        ok = false;
    } else {
        cout << "PASS: DST_ACC limpia el acumulador de pe(1,0) (SRC_ACC lo confirma en 0 en out_E[1])" << endl;
    }

    Link row2_after = out_E[2].read();
    Link row2_delta = lane_delta(row2_before, row2_after);
    Link row2_expected = Link({
        a2k2[0] * cycles_elapsed, a2k2[1] * cycles_elapsed,
        a2k2[2] * cycles_elapsed, a2k2[3] * cycles_elapsed});
    if (row2_delta != row2_expected) {
        cout << "FAIL: fila 2 se interrumpio mientras se reseteaba pe(1,0), esperaba delta "
             << row2_expected << ", obtuve " << row2_delta << endl;
        ok = false;
    } else {
        cout << "PASS: fila 2 siguio acumulando sin interrupcion (" << row2_delta
             << " en " << cycles_elapsed << " ciclos) mientras se reseteaba pe(1,0)" << endl;
    }

    sc_close_vcd_trace_file(tf);
    return ok ? 0 : 1;
}
