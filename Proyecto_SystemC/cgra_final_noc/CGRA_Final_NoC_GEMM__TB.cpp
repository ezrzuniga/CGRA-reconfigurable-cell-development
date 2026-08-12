// CGRA_Final_NoC_GEMM__TB.cpp
// Puerto directo de cgra_final_TB/CGRA_Final_GEMM__TB.cpp a CGRA_Final_NoC_Mesh:
// MISMO programa espacial, MISMOS margenes de ciclos, MISMA secuencia de
// feed_k_step -- la unica diferencia real con el original es el include y los
// typedefs (Mesh/Link/Instr/constantes _NOC_). Ese es exactamente el punto de
// esta prueba: si un programa de GEMM 2x2 escrito y afinado ciclo a ciclo
// para la malla de wires directos (CGRA_Final_Mesh) corre SIN CAMBIOS sobre
// la malla NoC y produce el mismo resultado, es la demostracion mas fuerte
// posible de que NoC_Router (combinacional, sin latencia extra -- ver
// NoC_Router.h) es un reemplazo de interconexion "drop-in", cycle-accurate
// identico, no solo funcionalmente equivalente.
//
// Ver cgra_final_TB/CGRA_Final_GEMM__TB.cpp para la explicacion completa del
// mapeo del algoritmo (bloque MAC sistolico embebido en la malla 3x3, relays
// de Routing/Vectorial/Escalar, etc.) -- no se repite aca.

#include <systemc.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include "CGRA_Final_NoC_Mesh.h"
#include "../pe_hls/test_util.h"

typedef CGRA_Final_NoC_Mesh Mesh;
typedef CGRA_Final_NoC_Link Link;
typedef CGRA_Final_NoC_Instr Instr;

static const int ROWS = CGRA_FINAL_NOC_ROWS;
static const int COLS = CGRA_FINAL_NOC_COLS;
static const int PROG_LEN = CGRA_FINAL_NOC_INSTR_MEM_SIZE;  // 16: tamano real de instr_mem de cada MAC
static const int PROG_SLOTS = 8;

struct Coord { int row, col; };
static const Coord MAC_CELL[2][2] = {
    {{1, 1}, {1, 2}},
    {{2, 1}, {2, 2}}
};

//============================================================================
// Constructores de instruccion
//============================================================================
static Instr mov_instr(sc_uint<3> src, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
static Instr mac_instr(sc_uint<3> src_a, sc_uint<3> src_b, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MAC; i.src_a = src_a; i.src_b = src_b; i.dst = dst; return i;
}
static Instr clear_acc_instr() {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_IMM; i.imm = 0; i.dst = DST_ACC; return i;
}
static Instr nop_instr() { return Instr(); }

static void build_gemm_program(Instr prog[2][2][PROG_SLOTS]) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int s = 0; s < PROG_SLOTS; s++)
                prog[i][j][s] = nop_instr();

    prog[0][0][1] = mov_instr(SRC_WEST, DST_EAST);                   // A -> P01
    prog[0][0][2] = mov_instr(SRC_NORTH, DST_SOUTH);                 // B -> P10
    prog[0][0][4] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][0][6] = mov_instr(SRC_ACC, DST_WEST);   // -> Routing(1,0) ctx1 -> out_W[1]

    prog[0][1][2] = mov_instr(SRC_NORTH, DST_SOUTH);                 // B -> P11
    prog[0][1][4] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][1][6] = mov_instr(SRC_ACC, DST_EAST);   // -> out_E[1]

    prog[1][0][1] = mov_instr(SRC_WEST, DST_EAST);                   // A -> P11
    prog[1][0][4] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][0][6] = mov_instr(SRC_ACC, DST_SOUTH);  // -> out_S[1] (borde real directo)

    prog[1][1][5] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][1][7] = mov_instr(SRC_ACC, DST_EAST);   // -> out_E[2]
}

static Instr routing_relay_in()  {
    return make_routing_config_instr_hls<CGRA_FINAL_NOC_DATA_W>(RC_NONE, RC_FROM_W, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {
    return make_routing_config_instr_hls<CGRA_FINAL_NOC_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Carga de programas (identico a cgra_final_TB/CGRA_Final_GEMM__TB.cpp)
//============================================================================
static void load_mac_program(Mesh& mesh, sc_signal<bool>& rst, const Instr prog[2][2][PROG_SLOTS]) {
    rst.write(true);
    advance_cycles(1);
    rst.write(false);

    for (int addr = 0; addr < PROG_LEN; addr++) {
        if (addr == 0) {
            mesh.load_instr(2, 0, 0, routing_relay_in());  // Routing(2,0): A -> fila 2
            mesh.load_instr(1, 0, 0, routing_relay_in());  // Routing(1,0): A -> fila 1 (modo entrada)
        }
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                mesh.load_instr(MAC_CELL[i][j].row, MAC_CELL[i][j].col, addr, prog[i][j][addr % PROG_SLOTS]);
        advance_cycles(1);
    }
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            mesh.clear_instr(MAC_CELL[i][j].row, MAC_CELL[i][j].col);
    mesh.clear_instr(2, 0);
    mesh.clear_instr(1, 0);
}

static void clear_mac_acc(Mesh& mesh) {
    for (int addr = 0; addr < PROG_LEN; addr++) {
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                mesh.load_instr(MAC_CELL[i][j].row, MAC_CELL[i][j].col, addr, clear_acc_instr());
        advance_cycles(1);
    }
    advance_cycles(1);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            mesh.clear_instr(MAC_CELL[i][j].row, MAC_CELL[i][j].col);
}

static void setup_b_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh.load_instr(0, 1, addr, relay);  // Vectorial
        mesh.load_instr(0, 2, addr, relay);  // Escalar
        advance_cycles(1);
    }
    mesh.clear_instr(0, 1);
    mesh.clear_instr(0, 2);
}

//============================================================================
// Un caso de prueba: A, B (2x2) y su etiqueta.
//============================================================================
struct GemmCase {
    std::string label;
    int32_t A[2][2];
    int32_t B[2][2];
};

static void print_matrix(std::ostream& os, const char* name, const int32_t M[2][2]) {
    os << "      " << name << " = [ " << std::setw(4) << M[0][0] << " " << std::setw(4) << M[0][1] << " ]\n"
       << "        " << std::string(strlen(name), ' ') << "   [ " << std::setw(4) << M[1][0] << " " << std::setw(4) << M[1][1] << " ]\n";
}

static void feed_k_step(sc_signal<bool>& rst, sc_signal<bool>& enable,
                         sc_signal<Link>* in_N, sc_signal<Link>* in_W,
                         sc_signal<Link>* out_E, sc_signal<Link>* out_S,
                         int32_t a_row0, int32_t a_row1, int32_t b_col0, int32_t b_col1) {
    in_W[1].write(Link({a_row0}));
    in_W[2].write(Link({a_row1}));
    in_N[1].write(Link({b_col0}));
    in_N[2].write(Link({b_col1}));
    for (int t = 0; t < PROG_SLOTS; t++) {
        advance_cycles(1);
    }
}

static bool run_case(Mesh& mesh, sc_signal<bool>& rst, sc_signal<bool>& enable, sc_signal<Link>* in_N, sc_signal<Link>* in_W,
                      sc_signal<Link>* out_W, sc_signal<Link>* out_E, sc_signal<Link>* out_S,
                      int case_num, const GemmCase& tc) {
    int32_t C[2][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += tc.A[i][k] * tc.B[k][j];
            C[i][j] = sum;
        }

    cout << "\n============================================================\n"
         << "  CASO " << case_num << " -- " << tc.label << " (sobre CGRA_Final_NoC_Mesh)\n"
         << "============================================================\n";
    print_matrix(cout, "A", tc.A);
    print_matrix(cout, "B", tc.B);
    cout << "      C esperado = [ " << std::setw(4) << C[0][0] << " " << std::setw(4) << C[0][1] << " ]\n"
         << "                   [ " << std::setw(4) << C[1][0] << " " << std::setw(4) << C[1][1] << " ]\n";

    test_section((std::string("Caso ") + std::to_string(case_num) + ": k=0").c_str());
    feed_k_step(rst, enable, in_N, in_W, out_E, out_S, tc.A[0][0], tc.A[1][0], tc.B[0][0], tc.B[0][1]);

    test_section((std::string("Caso ") + std::to_string(case_num) + ": k=1").c_str());
    feed_k_step(rst, enable, in_N, in_W, out_E, out_S, tc.A[0][1], tc.A[1][1], tc.B[1][0], tc.B[1][1]);

    mesh.load_instr(1, 0, 1, routing_relay_out());
    advance_cycles(2);
    mesh.clear_instr(1, 0);

    int32_t got[2][2] = {
        { (int32_t)out_W[1].read()[0], (int32_t)out_E[1].read()[0] },
        { (int32_t)out_S[1].read()[0], (int32_t)out_E[2].read()[0] }
    };

    bool ok = true;
    cout << "\n      C obtenido = [ " << std::setw(4) << got[0][0] << " " << std::setw(4) << got[0][1] << " ]\n"
         << "                   [ " << std::setw(4) << got[1][0] << " " << std::setw(4) << got[1][1] << " ]\n\n";
    static const char* labels[2][2] = {{"C[0][0] (out_W[1], via Routing)", "C[0][1] (out_E[1])"},
                                        {"C[1][0] (out_S[1])",             "C[1][1] (out_E[2])"}};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            std::ostringstream in;
            in << "A,B del caso " << case_num;
            test_check(ok, labels[i][j], in.str(), C[i][j], got[i][j]);
        }

    cout << "  Resultado del caso " << case_num << ": " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

static void gen_random_matrix(int32_t M[2][2], int lo, int hi) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            M[i][j] = lo + std::rand() % (hi - lo + 1);
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

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

    cout << "\n############################################################\n"
         << "#  GEMM 2x2 sobre CGRA_Final_NoC_Mesh (layout 3x3, interconexion NoC)  #\n"
         << "#  (0,0)=Memoria (0,1)=Vectorial (0,2)=Escalar               #\n"
         << "#  (1,0)=Routing (1,1)=MAC       (1,2)=MAC                   #\n"
         << "#  (2,0)=Routing (2,1)=MAC       (2,2)=MAC                   #\n"
         << "############################################################\n";

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    advance_cycles(2);
    rst.write(false);
    enable.write(true);

    Instr prog[2][2][PROG_SLOTS];
    build_gemm_program(prog);

    test_section("Programacion inicial: relays de B (Vectorial/Escalar) + relays de A y programa MAC (Routing + bloque sistolico)");
    setup_b_relays(mesh);
    load_mac_program(mesh, rst, prog);
    advance_cycles(PROG_LEN);

    std::srand(20260810);
    GemmCase cases[3];
    cases[0].label = "matrices aleatorias #1";
    cases[1].label = "matrices aleatorias #2";
    cases[2].label = "matrices aleatorias #3";
    for (int c = 0; c < 3; c++) {
        gen_random_matrix(cases[c].A, -9, 9);
        gen_random_matrix(cases[c].B, -9, 9);
    }

    bool all_ok = true;
    for (int c = 0; c < 3; c++) {
        if (c > 0) {
            test_section((std::string("Entre casos: limpiar acumuladores + recargar programa (caso ") +
                          std::to_string(c + 1) + ")").c_str());
            clear_mac_acc(mesh);
            load_mac_program(mesh, rst, prog);
        }
        bool ok = run_case(mesh, rst, enable, in_N, in_W, out_W, out_E, out_S, c + 1, cases[c]);
        all_ok = all_ok && ok;
    }

    cout << "\n############################################################\n";
    if (all_ok) {
        cout << "#  PASS: GEMM 2x2 correcto en los 3 casos sobre CGRA_Final_NoC_Mesh  #\n";
    } else {
        cout << "#  FAIL: al menos un caso no coincidio (ver detalle arriba)  #\n";
    }
    cout << "############################################################\n";

    return all_ok ? 0 : 1;
}
