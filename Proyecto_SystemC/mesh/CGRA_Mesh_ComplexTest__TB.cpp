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
// Tras 3 transiciones de entrada, las 9 PEs se reprograman con instrucciones
// nuevas (datos ya fluyendo -- nunca ejercitado antes a nivel de malla) y
// reciben otras 3 transiciones contra el programa nuevo.

#include <systemc.h>
#include <sstream>
#include "CGRA_Mesh_Heterogeneous.h"
#include "../pe/test_util.h"

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

    // layout es plano y row-major (indice = r*COLS+c); su tamano debe calzar
    // exacto con ROWS*COLS (ver mesh/README.md, seccion "Definir un layout").
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

    // load_instr necesita un flanco de clk para latchear; addr=0 en todas
    // porque esta malla usa INSTR_MEM_SIZE=1 (ver mesh/README.md).
    mesh.load_instr(0, 0, 0, add_c);
    mesh.load_instr(0, 1, 0, mul_e);
    mesh.load_instr(0, 2, 0, and_g);
    mesh.load_instr(1, 0, 0, add_c_vec);
    mesh.load_instr(1, 1, 0, mul_e_vec);
    mesh.load_instr(1, 2, 0, and_g_vec);
    mesh.load_instr(2, 0, 0, mac_acc);
    mesh.load_instr(2, 1, 0, relay);
    mesh.load_instr(2, 2, 0, relay);
    advance_cycles(1);
    mesh.clear_instr(0, 0); mesh.clear_instr(0, 1); mesh.clear_instr(0, 2);
    mesh.clear_instr(1, 0); mesh.clear_instr(1, 1); mesh.clear_instr(1, 2);
    mesh.clear_instr(2, 0); mesh.clear_instr(2, 1); mesh.clear_instr(2, 2);
    cout << "fila 0: ADD->MUL->AND (b/d/f=in_N) | fila 1: ADD->MUL->AND (k1="
         << add_c_vec.imm << " k2=" << mul_e_vec.imm << " k3=" << and_g_vec.imm
         << ") | fila 2: MAC(imm=" << mac_acc.imm << ") + 2 relevos" << endl;

    bool ok = true;

    test_section("Estimulo inicial");

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
    advance_cycles(5);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0]
           << " in_N[1](d)=" << in_N[1].read()[0] << " in_N[2](f)=" << in_N[2].read()[0];
        Link expected0({4, 4, 4, 4});
        test_check_scalar(ok, "fila 0 (PE_scalar): pipeline g=(a+b)*d & f", in.str(), expected0, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " k1=" << add_c_vec.imm
           << " k2=" << mul_e_vec.imm << " k3=" << and_g_vec.imm;
        Link expected1({4, 0, 12, 8});
        test_check(ok, "fila 1 (PE_vector): pipeline g=(a+7)*3 & 15", in.str(), expected1, out_E[1].read());
    }

    // Fila 2: el acumulador sigue creciendo cada ciclo, se verifica por delta.
    Link snap2_before = out_E[2].read();
    advance_cycles(1);
    Link snap2_after = out_E[2].read();
    Link delta2 = lane_delta(snap2_before, snap2_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc.imm
           << " (delta en 1 ciclo, tras 2 saltos de relevo)";
        test_check(ok, "fila 2 (PE_MAC): acc += a * k", in.str(), a2k2, delta2);
    }

    // Verificacion sostenida: el delta de 1 ciclo ya probado arriba es solo una
    // muestra; esto confirma que el acumulador mantiene la MISMA tasa varios
    // ciclos seguidos con el estimulo constante (no solo que acerto una vez),
    // ya con los 2 saltos de relevo asentados.
    {
        const int N = 4;
        Link prev = out_E[2].read();
        bool sustained = true;
        for (int i = 0; i < N; ++i) {
            advance_cycles(1);
            Link now = out_E[2].read();
            if (lane_delta(prev, now) != a2k2) sustained = false;
            prev = now;
        }
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc.imm
           << " (mismo delta esperado los " << N << " ciclos: " << a2k2 << ")";
        test_check_bool(ok, "fila 2 (MAC sostenido, " + std::to_string(N) + " ciclos seguidos)", in.str(), sustained);
    }

    test_section("Segunda transicion");
    // Confirma que los 3 pipelines reaccionan a un cambio de entrada DESPUES
    // del arranque (Etapa 0 del plan de mejora de verificacion), no solo al
    // valor inicial con el que se armo cada corrida.
    // Fila 0 necesita el mismo margen que el arranque en frio (5 ciclos, no 3):
    // cada salto entre celdas PE_Scalar_Cell pasa por bridge_out_E de la celda
    // origen y bridge_in_W de la destino (PE_Scalar_Cell.h), a diferencia del
    // binding directo de PE_Vector_Cell -- ver hallazgo equivalente (1 celda,
    // 2 ciclos en vez de 1) en CGRA_Mesh_SmokeTest__TB.cpp.
    in_W[0].write(Link({8, 8, 8, 8}));   // b/d/f (in_N) quedan igual: 7,3,15
    in_W[1].write(Link({2, 4, 6, 8}));
    advance_cycles(5);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0]
           << " in_N[1](d)=" << in_N[1].read()[0] << " in_N[2](f)=" << in_N[2].read()[0]
           << " (b/d/f sin cambios)";
        Link expected0b({13, 13, 13, 13});
        test_check_scalar(ok, "fila 0 (2da transicion): pipeline g=(a+b)*d & f", in.str(), expected0b, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " k1=" << add_c_vec.imm
           << " k2=" << mul_e_vec.imm << " k3=" << and_g_vec.imm;
        Link expected1b({11, 1, 7, 13});
        test_check(ok, "fila 1 (2da transicion): pipeline g=(a+7)*3 & 15", in.str(), expected1b, out_E[1].read());
    }

    // Fila 2: nuevo "a" constante -> nuevo ritmo de acumulacion por ciclo.
    Link a2_new({2, 2, 2, 2});
    Link a2k2_new({4, 4, 4, 4});
    in_W[2].write(a2_new);
    advance_cycles(3);  // deja re-asentar los 2 saltos de relevo con el nuevo dato.
    Link snap2b_before = out_E[2].read();
    advance_cycles(1);
    Link snap2b_after = out_E[2].read();
    Link delta2b = lane_delta(snap2b_before, snap2b_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc.imm << " (delta en 1 ciclo)";
        test_check(ok, "fila 2 (2da transicion): acc siguio el nuevo a", in.str(), a2k2_new, delta2b);
    }

    test_section("Tercera transicion");
    in_W[0].write(Link({10, 10, 10, 10}));  // b/d/f (in_N) siguen en 7,3,15
    in_W[1].write(Link({3, 12, 20, 1}));
    advance_cycles(5);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0]
           << " in_N[1](d)=" << in_N[1].read()[0] << " in_N[2](f)=" << in_N[2].read()[0];
        Link expected0c({3, 3, 3, 3});
        test_check_scalar(ok, "fila 0 (3ra transicion): pipeline g=(a+b)*d & f", in.str(), expected0c, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " k1=" << add_c_vec.imm
           << " k2=" << mul_e_vec.imm << " k3=" << and_g_vec.imm;
        Link expected1c({14, 9, 1, 8});
        test_check(ok, "fila 1 (3ra transicion): pipeline g=(a+7)*3 & 15", in.str(), expected1c, out_E[1].read());
    }

    Link a2_c({3, 3, 3, 3});
    Link a2k2_c({6, 6, 6, 6});
    in_W[2].write(a2_c);
    advance_cycles(3);
    Link snap2c_before = out_E[2].read();
    advance_cycles(1);
    Link snap2c_after = out_E[2].read();
    Link delta2c = lane_delta(snap2c_before, snap2c_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc.imm << " (delta en 1 ciclo)";
        test_check(ok, "fila 2 (3ra transicion): acc siguio el nuevo a", in.str(), a2k2_c, delta2c);
    }

    test_section("Reprogramacion");
    // Reprograma las 9 PEs con datos ya fluyendo (nunca ejercitado antes a nivel
    // de malla, ver pe/CLAUDE.md "Nota de timing relacionada" bajo PE_MAC). Fila 0
    // y fila 1 mantienen la misma estructura de ruteo (pipeline de 3 etapas) pero
    // cambian de opcode -- prueba que reprogramar celdas que ya forman parte de un
    // pipeline activo no rompe nada. Los 2 relevos de la fila 2 se recargan con la
    // misma instruccion que ya tenian, para honrar "todas las PEs" aunque su
    // comportamiento no cambie.
    Mesh::Instr sub_c2;
    sub_c2.opcode = OP_SUB; sub_c2.src_a = SRC_WEST; sub_c2.src_b = SRC_NORTH; sub_c2.dst = DST_EAST;
    Mesh::Instr or_e2;
    or_e2.opcode = OP_OR; or_e2.src_a = SRC_WEST; or_e2.src_b = SRC_NORTH; or_e2.dst = DST_EAST;
    Mesh::Instr xor_g2;
    xor_g2.opcode = OP_XOR; xor_g2.src_a = SRC_WEST; xor_g2.src_b = SRC_NORTH; xor_g2.dst = DST_EAST;

    Mesh::Instr sub_c_vec2;
    sub_c_vec2.opcode = OP_SUB; sub_c_vec2.src_a = SRC_WEST; sub_c_vec2.src_b = SRC_IMM; sub_c_vec2.imm = 2; sub_c_vec2.dst = DST_EAST;
    Mesh::Instr or_e_vec2;
    or_e_vec2.opcode = OP_OR; or_e_vec2.src_a = SRC_WEST; or_e_vec2.src_b = SRC_IMM; or_e_vec2.imm = 6; or_e_vec2.dst = DST_EAST;
    Mesh::Instr xor_g_vec2;
    xor_g_vec2.opcode = OP_XOR; xor_g_vec2.src_a = SRC_WEST; xor_g_vec2.src_b = SRC_IMM; xor_g_vec2.imm = 9; xor_g_vec2.dst = DST_EAST;

    Mesh::Instr mac_acc2;
    mac_acc2.opcode = OP_MAC; mac_acc2.src_a = SRC_WEST; mac_acc2.src_b = SRC_IMM; mac_acc2.imm = 3; mac_acc2.dst = DST_EAST;

    mesh.load_instr(0, 0, 0, sub_c2);
    mesh.load_instr(0, 1, 0, or_e2);
    mesh.load_instr(0, 2, 0, xor_g2);
    mesh.load_instr(1, 0, 0, sub_c_vec2);
    mesh.load_instr(1, 1, 0, or_e_vec2);
    mesh.load_instr(1, 2, 0, xor_g_vec2);
    mesh.load_instr(2, 0, 0, mac_acc2);
    mesh.load_instr(2, 1, 0, relay);
    mesh.load_instr(2, 2, 0, relay);
    // 3 ciclos en vez de 1: reprogramar una celda PE_MAC en caliente necesita mas
    // margen (bridge_instr_in async extra, ver pe/CLAUDE.md).
    advance_cycles(3);
    mesh.clear_instr(0, 0); mesh.clear_instr(0, 1); mesh.clear_instr(0, 2);
    mesh.clear_instr(1, 0); mesh.clear_instr(1, 1); mesh.clear_instr(1, 2);
    mesh.clear_instr(2, 0); mesh.clear_instr(2, 1); mesh.clear_instr(2, 2);
    // Deja repropagar los pipelines de 3 etapas con las instrucciones nuevas --
    // mismo margen que una transicion de estimulo (Segunda/Tercera transicion),
    // porque los 3 valores intermedios del pipeline tienen que recalcularse igual.
    advance_cycles(5);
    cout << "fila 0: SUB->OR->XOR (b/d/f=in_N) | fila 1: SUB->OR->XOR (k1="
         << sub_c_vec2.imm << " k2=" << or_e_vec2.imm << " k3=" << xor_g_vec2.imm
         << ") | fila 2: MAC(imm=" << mac_acc2.imm << ") + 2 relevos" << endl;

    // Checks aislados: mismo estimulo que la 3ra transicion (sin cambiarlo
    // todavia), formula NUEVA -- separa "did la reprogramacion tomar efecto" de
    // "reacciona la malla a un estimulo nuevo".
    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0]
           << " in_N[1](d)=" << in_N[1].read()[0] << " in_N[2](f)=" << in_N[2].read()[0]
           << " (estimulo sin cambios)";
        Link expected0d({12, 12, 12, 12});
        test_check_scalar(ok, "fila 0 (post-reprogramacion): pipeline g=(a-b)|d ^ f", in.str(), expected0d, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " k1=" << sub_c_vec2.imm
           << " k2=" << or_e_vec2.imm << " k3=" << xor_g_vec2.imm << " (estimulo sin cambios)";
        Link expected1d({14, 7, 31, -10});
        test_check(ok, "fila 1 (post-reprogramacion): pipeline g=(a-2)|6 ^ 9", in.str(), expected1d, out_E[1].read());
    }

    Link snap2d_before = out_E[2].read();
    advance_cycles(1);
    Link snap2d_after = out_E[2].read();
    Link delta2d = lane_delta(snap2d_before, snap2d_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc2.imm
           << " (estimulo sin cambios, delta en 1 ciclo)";
        Link expected_delta2d({9, 9, 9, 9});
        test_check(ok, "fila 2 (post-reprogramacion): acc += a * k (nuevo imm)", in.str(), expected_delta2d, delta2d);
    }

    test_section("Transicion post-reprogramacion 1");
    in_W[0].write(Link({20, 20, 20, 20}));
    in_W[1].write(Link({5, 15, 25, 35}));
    advance_cycles(5);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0]
           << " in_N[1](d)=" << in_N[1].read()[0] << " in_N[2](f)=" << in_N[2].read()[0];
        Link expected0e({0, 0, 0, 0});
        test_check_scalar(ok, "fila 0 (post-reprog, transicion 1): pipeline g=(a-b)|d ^ f", in.str(), expected0e, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " k1=" << sub_c_vec2.imm
           << " k2=" << or_e_vec2.imm << " k3=" << xor_g_vec2.imm;
        Link expected1e({14, 6, 30, 46});
        test_check(ok, "fila 1 (post-reprog, transicion 1): pipeline g=(a-2)|6 ^ 9", in.str(), expected1e, out_E[1].read());
    }

    Link a2_e({5, 5, 5, 5});
    Link a2k2_e({15, 15, 15, 15});
    in_W[2].write(a2_e);
    advance_cycles(3);
    Link snap2e_before = out_E[2].read();
    advance_cycles(1);
    Link snap2e_after = out_E[2].read();
    Link delta2e = lane_delta(snap2e_before, snap2e_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc2.imm << " (delta en 1 ciclo)";
        test_check(ok, "fila 2 (post-reprog, transicion 1): acc += a * k", in.str(), a2k2_e, delta2e);
    }

    test_section("Transicion post-reprogramacion 2");
    in_W[0].write(Link({0, 0, 0, 0}));
    in_W[1].write(Link({-2, -10, -18, -26}));
    advance_cycles(5);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0]
           << " in_N[1](d)=" << in_N[1].read()[0] << " in_N[2](f)=" << in_N[2].read()[0];
        Link expected0f({-12, -12, -12, -12});
        test_check_scalar(ok, "fila 0 (post-reprog, transicion 2): pipeline g=(a-b)|d ^ f", in.str(), expected0f, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " k1=" << sub_c_vec2.imm
           << " k2=" << or_e_vec2.imm << " k3=" << xor_g_vec2.imm;
        Link expected1f({-9, -1, -25, -17});
        test_check(ok, "fila 1 (post-reprog, transicion 2): pipeline g=(a-2)|6 ^ 9", in.str(), expected1f, out_E[1].read());
    }

    Link a2_f({1, 1, 1, 1});
    Link a2k2_f({3, 3, 3, 3});
    in_W[2].write(a2_f);
    advance_cycles(3);
    Link snap2f_before = out_E[2].read();
    advance_cycles(1);
    Link snap2f_after = out_E[2].read();
    Link delta2f = lane_delta(snap2f_before, snap2f_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc2.imm << " (delta en 1 ciclo)";
        test_check(ok, "fila 2 (post-reprog, transicion 2): acc += a * k", in.str(), a2k2_f, delta2f);
    }

    test_section("Transicion post-reprogramacion 3");
    in_W[0].write(Link({-15, -15, -15, -15}));
    in_W[1].write(Link({40, 50, 60, 70}));
    advance_cycles(5);

    {
        std::ostringstream in;
        in << "in_W[0](a)=" << in_W[0].read()[0] << " in_N[0](b)=" << in_N[0].read()[0]
           << " in_N[1](d)=" << in_N[1].read()[0] << " in_N[2](f)=" << in_N[2].read()[0];
        Link expected0g({-28, -28, -28, -28});
        test_check_scalar(ok, "fila 0 (post-reprog, transicion 3): pipeline g=(a-b)|d ^ f", in.str(), expected0g, out_E[0].read());
    }

    {
        std::ostringstream in;
        in << "in_W[1](a)=" << in_W[1].read() << " k1=" << sub_c_vec2.imm
           << " k2=" << or_e_vec2.imm << " k3=" << xor_g_vec2.imm;
        Link expected1g({47, 63, 55, 79});
        test_check(ok, "fila 1 (post-reprog, transicion 3): pipeline g=(a-2)|6 ^ 9", in.str(), expected1g, out_E[1].read());
    }

    Link a2_g({6, 6, 6, 6});
    Link a2k2_g({18, 18, 18, 18});
    in_W[2].write(a2_g);
    advance_cycles(3);
    Link snap2g_before = out_E[2].read();
    advance_cycles(1);
    Link snap2g_after = out_E[2].read();
    Link delta2g = lane_delta(snap2g_before, snap2g_after);
    {
        std::ostringstream in;
        in << "in_W[2](a)=" << in_W[2].read() << " imm(k)=" << mac_acc2.imm << " (delta en 1 ciclo)";
        test_check(ok, "fila 2 (post-reprog, transicion 3): acc += a * k", in.str(), a2k2_g, delta2g);
    }

    sc_close_vcd_trace_file(tf);
    return ok ? 0 : 1;
}
