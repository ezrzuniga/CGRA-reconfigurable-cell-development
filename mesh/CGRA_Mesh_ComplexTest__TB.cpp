// CGRA_Mesh_ComplexTest__TB.cpp
// Malla 3x3 heterogenea, un tipo de PE por fila: fila 0 reproduce tal cual el
// pipeline escalar de CGRA_Mesh_MAC_Heterogeneous__TB.cpp (mismo programa,
// mismo estimulo), fila 2 reproduce tal cual una de sus filas MAC (acumula +
// verifica por delta, sin el ejercicio SRC_ACC/DST_ACC de aislamiento entre
// dos filas MAC -- ya no aplica con una sola fila MAC en este test). Lo
// nuevo es la fila 1: un pipeline VECTORIAL de 3 etapas, analogo al de la
// fila 0 (misma secuencia de opcodes ADD->MUL->AND encadenada
// horizontalmente entre 3 PEs), pero con una diferencia topologica: la fila
// 1 no es borde de la malla (ni r==0 ni r==ROWS-1), asi que su in_N/in_S son
// enlaces INTERNOS hacia las filas 0 y 2 -- usarlos como segundo operando
// acoplaria los 3 pipelines entre si, justo lo que
// CGRA_Mesh_Pipeline_Heterogeneous__TB.cpp ya evita a proposito. Por eso el
// segundo operando de cada etapa de la fila 1 usa SRC_IMM en vez de
// SRC_NORTH, igual que ya hace la fila MAC con su propio segundo operando.

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
        CellKind::VECTOR, CellKind::VECTOR, CellKind::VECTOR,  // fila 1
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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_complex_test_wave");
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

    // ---- Fila 0 (PE_scalar): c=a+b, e=c*d, g=e&f, segundo operando SRC_NORTH ----
    // Borde real de la malla (r==0), asi que cada etapa puede tomar b/d/f de
    // in_N[c] ademas de encadenar SRC_WEST->DST_EAST entre etapas.
    Mesh::Instr add_c;
    add_c.opcode = OP_ADD; add_c.src_a = SRC_WEST; add_c.src_b = SRC_NORTH; add_c.dst = DST_EAST;
    Mesh::Instr mul_e;
    mul_e.opcode = OP_MUL; mul_e.src_a = SRC_WEST; mul_e.src_b = SRC_NORTH; mul_e.dst = DST_EAST;
    Mesh::Instr and_g;
    and_g.opcode = OP_AND; and_g.src_a = SRC_WEST; and_g.src_b = SRC_NORTH; and_g.dst = DST_EAST;

    // ---- Fila 1 (PE_vector): c=a+k1, e=c*k2, g=e&k3, segundo operando SRC_IMM ----
    // Fila interior (ni r==0 ni r==ROWS-1): in_N[1]/in_S[1] son enlaces
    // internos hacia las filas 0/2, no borde real -- usarlos acoplaria los 3
    // pipelines. SRC_IMM mantiene la fila 1 autocontenida.
    Mesh::Instr add_c_vec;
    add_c_vec.opcode = OP_ADD; add_c_vec.src_a = SRC_WEST; add_c_vec.src_b = SRC_IMM; add_c_vec.imm = 7; add_c_vec.dst = DST_EAST;
    Mesh::Instr mul_e_vec;
    mul_e_vec.opcode = OP_MUL; mul_e_vec.src_a = SRC_WEST; mul_e_vec.src_b = SRC_IMM; mul_e_vec.imm = 3; mul_e_vec.dst = DST_EAST;
    Mesh::Instr and_g_vec;
    and_g_vec.opcode = OP_AND; and_g_vec.src_a = SRC_WEST; and_g_vec.src_b = SRC_IMM; and_g_vec.imm = 15; and_g_vec.dst = DST_EAST;

    // ---- Fila 2 (PE_MAC): acc += a*k en pe(2,0), pe(2,1)/pe(2,2) relevan ----
    Mesh::Instr relay;
    relay.opcode = OP_MOV; relay.src_a = SRC_WEST; relay.dst = DST_EAST;

    Mesh::Instr mac_acc;
    mac_acc.opcode = OP_MAC; mac_acc.src_a = SRC_WEST; mac_acc.src_b = SRC_IMM; mac_acc.imm = 2; mac_acc.dst = DST_EAST;
    Link a2({1, 1, 1, 1});
    Link a2k2({2, 2, 2, 2});

    mesh.load_instr(0, 0, 0, add_c);
    mesh.load_instr(0, 1, 0, mul_e);
    mesh.load_instr(0, 2, 0, and_g);
    mesh.load_instr(1, 0, 0, add_c_vec);
    mesh.load_instr(1, 1, 0, mul_e_vec);
    mesh.load_instr(1, 2, 0, and_g_vec);
    mesh.load_instr(2, 0, 0, mac_acc);
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

    // Estimulo fila 1: a={5,9,13,17} (lanes no uniformes, prueba SIMD real).
    // c=a+7={12,16,20,24}; e=c*3={36,48,60,72}; g=e&15=e mod 16={4,0,12,8}.
    in_W[1].write(Link({5, 9, 13, 17}));

    // Estimulo fila 2: constante durante toda la corrida.
    in_W[2].write(a2);

    // Asienta filas 0 y 1, y la propagacion inicial de los relevos de la
    // fila 2 (2 saltos hasta el borde este).
    sc_start(50, SC_NS);

    bool ok = true;

    Link expected0({4, 4, 4, 4});
    Link got0 = out_E[0].read();
    if (got0 != expected0) {
        cout << "FAIL fila 0: esperaba " << expected0 << ", obtuve " << got0 << endl;
        ok = false;
    } else {
        cout << "PASS fila 0 (PE_scalar): pipeline g=(a+b)*d & f = " << got0 << endl;
    }

    Link expected1({4, 0, 12, 8});
    Link got1 = out_E[1].read();
    if (got1 != expected1) {
        cout << "FAIL fila 1: esperaba " << expected1 << ", obtuve " << got1 << endl;
        ok = false;
    } else {
        cout << "PASS fila 1 (PE_vector): pipeline g=(a+7)*3 & 15 = " << got1 << endl;
    }

    // Fila 2: el acumulador sigue creciendo cada ciclo, se verifica por delta.
    Link snap2_before = out_E[2].read();
    sc_start(10, SC_NS);
    Link snap2_after = out_E[2].read();
    Link delta2 = lane_delta(snap2_before, snap2_after);
    if (delta2 != a2k2) {
        cout << "FAIL fila 2: esperaba delta " << a2k2 << " en 1 ciclo, obtuve " << delta2 << endl;
        ok = false;
    } else {
        cout << "PASS fila 2 (PE_MAC): acc += a * k crecio " << a2k2 << " en 1 ciclo" << endl;
    }

    sc_close_vcd_trace_file(tf);
    return ok ? 0 : 1;
}
