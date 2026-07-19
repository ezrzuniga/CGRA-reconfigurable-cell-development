// CGRA_Mesh_SmokeTest__TB.cpp
// Resumen rapido de las 3 variantes de PE en una sola malla minima: 3 filas x 1
// columna, una PE de cada tipo, cada una corriendo la operacion que mejor la
// representa:
//   fila 0 PE_scalar: c = a + b          (OP_ADD)
//   fila 1 PE_vector : e = a * k         (OP_MUL, k en broadcast via SRC_IMM)
//   fila 2 PE_MAC    : acc += a * k      (OP_MAC, acumula 1 ciclo por ciclo)
// Con COLS=1 cada fila tiene su in_W[r]/out_E[r] como borde REAL de la malla (ver
// CGRA_Mesh_Heterogeneous.h), asi que las 3 PEs se estimulan y se leen de forma
// independiente, sin celdas de relevo. Cada fila recibe 3 transiciones de entrada
// mid-run, luego las 3 PEs se reprograman con instrucciones nuevas (datos ya
// fluyendo -- nunca ejercitado antes a nivel de malla) y reciben otras 3
// transiciones contra el programa nuevo (Etapa 0 del plan de mejora de
// verificacion, mas la extension de reprogramacion en caliente).

#include <systemc.h>
#include <sstream>
#include "CGRA_Mesh_Heterogeneous.h"
#include "../pe/test_util.h"

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

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    advance_cycles(2);
    cout << "rst=1 enable=0 durante 2 ciclos, entradas en 0" << endl;

    rst.write(false);
    enable.write(true);

    test_section("Carga de instrucciones");

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
    advance_cycles(1);
    mesh.clear_instr(0, 0);
    mesh.clear_instr(1, 0);
    mesh.clear_instr(2, 0);
    cout << "fila 0: ADD(a,b)->E | fila 1: MUL(a,imm=" << mul_e.imm
         << ")->E | fila 2: MAC(a,imm=" << mac_acc.imm << ")->E" << endl;

    bool ok = true;

    test_section("Estimulo inicial");

    // Estimulo: a=5,b=7 -> c=12 (broadcast, celda escalar).
    in_W[0].write(Link({5, 5, 5, 5}));
    in_N[0].write(Link({7, 7, 7, 7}));

    // Estimulo: a={1,2,3,4} (lanes no uniformes), k=3 -> e={3,6,9,12}.
    in_W[1].write(Link({1, 2, 3, 4}));

    // Estimulo: a={2,3,4,5} constante, k=10 -> acc crece {20,30,40,50}/ciclo.
    in_W[2].write(Link({2, 3, 4, 5}));

    // Deja asentar la carga de instrucciones y el primer ciclo de computo.
    advance_cycles(2);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0];
        Link expected0({12, 12, 12, 12});
        test_check_scalar(ok, "fila 0 (PE_scalar): c = a + b", in.str(), expected0, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " imm(k)=" << mul_e.imm;
        Link expected1({3, 6, 9, 12});
        test_check(ok, "fila 1 (PE_vector): e = a * k (broadcast)", in.str(), expected1, out_E[1].read());
    }

    // Fila 2: el acumulador sigue creciendo cada ciclo, se verifica por delta.
    Link snapshot1 = out_E[2].read();
    advance_cycles(1);
    Link snapshot2 = out_E[2].read();
    Link delta2 = lane_delta(snapshot1, snapshot2);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc.imm << " (delta en 1 ciclo)";
        Link expected_delta2({20, 30, 40, 50});
        test_check(ok, "fila 2 (PE_MAC): acc += a * k", in.str(), expected_delta2, delta2);
    }

    // Verificacion sostenida: el delta de 1 ciclo ya probado arriba es solo una
    // muestra; esto confirma que el acumulador mantiene la MISMA tasa varios
    // ciclos seguidos con el estimulo constante (no solo que acerto una vez).
    {
        const int N = 4;
        Link rate({20, 30, 40, 50});
        Link prev = out_E[2].read();
        bool sustained = true;
        for (int i = 0; i < N; ++i) {
            advance_cycles(1);
            Link now = out_E[2].read();
            if (lane_delta(prev, now) != rate) sustained = false;
            prev = now;
        }
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc.imm
           << " (mismo delta esperado los " << N << " ciclos: " << rate << ")";
        test_check_bool(ok, "fila 2 (MAC sostenido, " + std::to_string(N) + " ciclos seguidos)", in.str(), sustained);
    }

    test_section("Segunda transicion");
    // Las instrucciones quedan residentes (PC hace loop en el unico slot), asi
    // que las 3 PEs corren su instruccion todos los ciclos; esto confirma que
    // reaccionan a un cambio de entrada DESPUES de la carga inicial. Fila 0
    // (PE_Scalar_Cell) necesita 2 ciclos en vez de 1: sus puertos de dato
    // pasan por bridge_in_N/bridge_in_W (PE_Scalar_Cell.h), a diferencia de
    // PE_Vector_Cell/PE_MAC_Cell que bindean directo; el ciclo extra es lo
    // que tarda esa indireccion en asentarse antes del siguiente flanco de
    // clk que usa issue().
    in_W[0].write(Link({10, 10, 10, 10}));
    in_N[0].write(Link({-3, -3, -3, -3}));
    in_W[1].write(Link({4, 5, 6, 7}));
    in_W[2].write(Link({1, 1, 1, 1}));
    advance_cycles(2);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0];
        Link expected0b({7, 7, 7, 7});
        test_check_scalar(ok, "fila 0 (2da transicion): c = a + b", in.str(), expected0b, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " imm(k)=" << mul_e.imm;
        Link expected1b({12, 15, 18, 21});
        test_check(ok, "fila 1 (2da transicion): e = a * k", in.str(), expected1b, out_E[1].read());
    }

    // Fila 2: nueva "a" -> nuevo ritmo de acumulacion, medido en su propia
    // ventana de 1 ciclo ya asentada (no comparte margen con la fila 0).
    Link snap2b_before = out_E[2].read();
    advance_cycles(1);
    Link snap2b_after = out_E[2].read();
    Link delta2b = lane_delta(snap2b_before, snap2b_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc.imm << " (delta en 1 ciclo)";
        Link expected_delta2b({10, 10, 10, 10});
        test_check(ok, "fila 2 (2da transicion): acc siguio el nuevo a", in.str(), expected_delta2b, delta2b);
    }

    test_section("Tercera transicion");
    in_W[0].write(Link({20, 20, 20, 20}));
    in_N[0].write(Link({5, 5, 5, 5}));
    in_W[1].write(Link({8, 6, 4, 2}));
    in_W[2].write(Link({3, 3, 3, 3}));
    advance_cycles(2);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0];
        Link expected0c({25, 25, 25, 25});
        test_check_scalar(ok, "fila 0 (3ra transicion): c = a + b", in.str(), expected0c, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " imm(k)=" << mul_e.imm;
        Link expected1c({24, 18, 12, 6});
        test_check(ok, "fila 1 (3ra transicion): e = a * k", in.str(), expected1c, out_E[1].read());
    }

    Link snap2c_before = out_E[2].read();
    advance_cycles(1);
    Link snap2c_after = out_E[2].read();
    Link delta2c = lane_delta(snap2c_before, snap2c_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc.imm << " (delta en 1 ciclo)";
        Link expected_delta2c({30, 30, 30, 30});
        test_check(ok, "fila 2 (3ra transicion): acc siguio el nuevo a", in.str(), expected_delta2c, delta2c);
    }

    test_section("Reprogramacion");
    // Reprograma las 3 PEs con datos ya fluyendo (nunca ejercitado antes a nivel
    // de malla, ver pe/CLAUDE.md "Nota de timing relacionada" bajo PE_MAC). Nuevas
    // instrucciones deliberadamente distintas de las anteriores para que un check
    // posterior solo pueda pasar si la reprogramacion realmente surtio efecto.
    Mesh::Instr sub_c;
    sub_c.opcode = OP_SUB;
    sub_c.src_a = SRC_WEST;
    sub_c.src_b = SRC_NORTH;
    sub_c.dst = DST_EAST;

    Mesh::Instr and_e;
    and_e.opcode = OP_AND;
    and_e.src_a = SRC_WEST;
    and_e.src_b = SRC_IMM;
    and_e.imm = 5;
    and_e.dst = DST_EAST;

    Mesh::Instr mac_acc2;
    mac_acc2.opcode = OP_MAC;
    mac_acc2.src_a = SRC_WEST;
    mac_acc2.src_b = SRC_IMM;
    mac_acc2.imm = 5;
    mac_acc2.dst = DST_EAST;

    mesh.load_instr(0, 0, 0, sub_c);
    mesh.load_instr(1, 0, 0, and_e);
    mesh.load_instr(2, 0, 0, mac_acc2);
    // 3 ciclos en vez de 1: reprogramar una celda PE_MAC en caliente necesita mas
    // margen (bridge_instr_in async extra, ver pe/CLAUDE.md); se usa el mismo
    // margen conservador para las 3 filas porque se reprograman en un solo lote.
    advance_cycles(3);
    mesh.clear_instr(0, 0);
    mesh.clear_instr(1, 0);
    mesh.clear_instr(2, 0);
    cout << "fila 0: SUB(a,b)->E | fila 1: AND(a,imm=" << and_e.imm
         << ")->E | fila 2: MAC(a,imm=" << mac_acc2.imm << ")->E" << endl;

    // Checks aislados: mismo estimulo que la 3ra transicion (sin cambiarlo
    // todavia), formula NUEVA -- separa "did la reprogramacion tomar efecto" de
    // "reacciona la malla a un estimulo nuevo", que son cosas distintas.
    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0]
           << " (estimulo sin cambios)";
        Link expected0d({15, 15, 15, 15});
        test_check_scalar(ok, "fila 0 (post-reprogramacion): c = a - b", in.str(), expected0d, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " imm(k)=" << and_e.imm << " (estimulo sin cambios)";
        Link expected1d({0, 4, 4, 0});
        test_check(ok, "fila 1 (post-reprogramacion): e = a & k", in.str(), expected1d, out_E[1].read());
    }

    Link snap2d_before = out_E[2].read();
    advance_cycles(1);
    Link snap2d_after = out_E[2].read();
    Link delta2d = lane_delta(snap2d_before, snap2d_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc2.imm
           << " (estimulo sin cambios, delta en 1 ciclo)";
        Link expected_delta2d({15, 15, 15, 15});
        test_check(ok, "fila 2 (post-reprogramacion): acc += a * k (nuevo imm)", in.str(), expected_delta2d, delta2d);
    }

    test_section("Transicion post-reprogramacion 1");
    in_W[0].write(Link({15, 15, 15, 15}));
    in_N[0].write(Link({4, 4, 4, 4}));
    in_W[1].write(Link({9, 10, 11, 12}));
    in_W[2].write(Link({2, 2, 2, 2}));
    advance_cycles(2);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0];
        Link expected0e({11, 11, 11, 11});
        test_check_scalar(ok, "fila 0 (post-reprog, transicion 1): c = a - b", in.str(), expected0e, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " imm(k)=" << and_e.imm;
        Link expected1e({1, 0, 1, 4});
        test_check(ok, "fila 1 (post-reprog, transicion 1): e = a & k", in.str(), expected1e, out_E[1].read());
    }

    Link snap2e_before = out_E[2].read();
    advance_cycles(1);
    Link snap2e_after = out_E[2].read();
    Link delta2e = lane_delta(snap2e_before, snap2e_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc2.imm << " (delta en 1 ciclo)";
        Link expected_delta2e({10, 10, 10, 10});
        test_check(ok, "fila 2 (post-reprog, transicion 1): acc += a * k", in.str(), expected_delta2e, delta2e);
    }

    test_section("Transicion post-reprogramacion 2");
    in_W[0].write(Link({0, 0, 0, 0}));
    in_N[0].write(Link({-8, -8, -8, -8}));
    in_W[1].write(Link({6, 7, 13, 20}));
    in_W[2].write(Link({4, 4, 4, 4}));
    advance_cycles(2);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0];
        Link expected0f({8, 8, 8, 8});
        test_check_scalar(ok, "fila 0 (post-reprog, transicion 2): c = a - b", in.str(), expected0f, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " imm(k)=" << and_e.imm;
        Link expected1f({4, 5, 5, 4});
        test_check(ok, "fila 1 (post-reprog, transicion 2): e = a & k", in.str(), expected1f, out_E[1].read());
    }

    Link snap2f_before = out_E[2].read();
    advance_cycles(1);
    Link snap2f_after = out_E[2].read();
    Link delta2f = lane_delta(snap2f_before, snap2f_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc2.imm << " (delta en 1 ciclo)";
        Link expected_delta2f({20, 20, 20, 20});
        test_check(ok, "fila 2 (post-reprog, transicion 2): acc += a * k", in.str(), expected_delta2f, delta2f);
    }

    test_section("Transicion post-reprogramacion 3");
    in_W[0].write(Link({-5, -5, -5, -5}));
    in_N[0].write(Link({-5, -5, -5, -5}));
    in_W[1].write(Link({31, 16, 3, 0}));
    in_W[2].write(Link({1, 1, 1, 1}));
    advance_cycles(2);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0];
        Link expected0g({0, 0, 0, 0});
        test_check_scalar(ok, "fila 0 (post-reprog, transicion 3): c = a - b", in.str(), expected0g, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " imm(k)=" << and_e.imm;
        Link expected1g({5, 0, 1, 0});
        test_check(ok, "fila 1 (post-reprog, transicion 3): e = a & k", in.str(), expected1g, out_E[1].read());
    }

    Link snap2g_before = out_E[2].read();
    advance_cycles(1);
    Link snap2g_after = out_E[2].read();
    Link delta2g = lane_delta(snap2g_before, snap2g_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc2.imm << " (delta en 1 ciclo)";
        Link expected_delta2g({5, 5, 5, 5});
        test_check(ok, "fila 2 (post-reprog, transicion 3): acc += a * k", in.str(), expected_delta2g, delta2g);
    }

    sc_close_vcd_trace_file(tf);
    return ok ? 0 : 1;
}
