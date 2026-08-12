// CGRA_Final_NoC_GEMM_C__TB.cpp
// Transliteracion a C/C++ puro de cgra_final_noc/CGRA_Final_NoC_GEMM__TB.cpp:
// puerto directo de cgra_final_TB_c/CGRA_Final_GEMM_C__TB.cpp a
// CGRA_Final_NoC_Mesh_C. MISMO programa espacial, MISMOS margenes de ciclos,
// MISMA secuencia de feed_k_step -- las unicas diferencias con el original son
// el include, los typedefs y que las salidas de borde se leen de los arreglos
// out_* de noc_mesh_step() en vez del campo out_X de la celda de borde.
//
// Ese es exactamente el punto de esta prueba: si un programa de GEMM 2x2
// escrito y afinado ciclo a ciclo para la malla de wires directos corre SIN
// CAMBIOS sobre la malla NoC y produce el mismo resultado, es la demostracion
// mas fuerte posible de que la fabrica de routers (combinacional, sin latencia
// extra -- ver NoC_Router_C.h / NoC_Mesh_Static_C.h) es un reemplazo de
// interconexion "drop-in", cycle-accurate identico, no solo funcionalmente
// equivalente.
//
// Ver cgra_final_TB_c/CGRA_Final_GEMM_C__TB.cpp para la explicacion completa
// del mapeo del algoritmo (bloque MAC sistolico embebido en la malla 3x3,
// relays de Routing/Vectorial/Escalar, calendario de slots) -- no se repite.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include "CGRA_Final_NoC_Mesh_C.h"
#include "../pe_hls_c/test_util_c.h"

typedef CGRA_Final_NoC_Mesh_C Mesh;
typedef CGRA_Final_NoC_Link   Link;
typedef CGRA_Final_NoC_Instr  Instr;

static const int ROWS   = CGRA_FINAL_ROWS;
static const int COLS   = CGRA_FINAL_COLS;
static const int DATA_W = CGRA_FINAL_DATA_W;
static const int PROG_LEN = CGRA_FINAL_INSTR_MEM_SIZE;  // 16
static const int PROG_SLOTS = 10;

struct Coord { int row, col; };
static const Coord MAC_CELL[2][2] = {
    {{1, 1}, {1, 2}},
    {{2, 1}, {2, 2}}
};

static Link g_in_N[COLS],  g_in_S[COLS],  g_in_W[ROWS],  g_in_E[ROWS];
static Link g_out_N[COLS], g_out_S[COLS], g_out_W[ROWS], g_out_E[ROWS];

static void step_n(Mesh& mesh, int n, bool rst = false) {
    for (int i = 0; i < n; i++)
        cgra_final_noc_step(mesh, rst, /*enable=*/true, g_in_N, g_in_S, g_in_W, g_in_E,
                            g_out_N, g_out_S, g_out_W, g_out_E);
    test_count_cycles(n);
}

static Link lane0(int32_t v) { Link l; l[0] = v; return l; }

//============================================================================
// Constructores de instruccion (identicos al TB de malla directa)
//============================================================================
static Instr mov_instr(ap_uint<3> src, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
static Instr mac_instr(ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MAC; i.src_a = src_a; i.src_b = src_b; i.dst = dst; return i;
}
static Instr nop_instr() { return Instr(); }

static void build_gemm_program(Instr prog[2][2][PROG_SLOTS]) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int s = 0; s < PROG_SLOTS; s++)
                prog[i][j][s] = nop_instr();

    // --- k = 0 -------------------------------------------------------------
    prog[0][0][1] = mov_instr(SRC_WEST, DST_EAST);                    // A00 -> P01
    prog[1][0][1] = mov_instr(SRC_WEST, DST_EAST);                    // A10 -> P11
    prog[0][0][2] = mov_instr(SRC_NORTH, DST_SOUTH);                  // B00 -> P10
    prog[0][1][2] = mov_instr(SRC_NORTH, DST_SOUTH);                  // B01 -> P11
    prog[0][0][3] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][1][3] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][0][3] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][1][3] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);

    // --- k = 1 (mismo patron, corrido 4 slots) -----------------------------
    prog[0][0][5] = mov_instr(SRC_WEST, DST_EAST);
    prog[1][0][5] = mov_instr(SRC_WEST, DST_EAST);
    prog[0][0][6] = mov_instr(SRC_NORTH, DST_SOUTH);
    prog[0][1][6] = mov_instr(SRC_NORTH, DST_SOUTH);
    prog[0][0][7] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][1][7] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][0][7] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][1][7] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);

    // --- lectura de acumuladores -------------------------------------------
    prog[0][0][8] = mov_instr(SRC_ACC, DST_WEST);   // -> Routing(1,0) ctx1 -> out_W[1]
    prog[0][1][8] = mov_instr(SRC_ACC, DST_EAST);   // -> out_E[1]
    prog[1][0][8] = mov_instr(SRC_ACC, DST_SOUTH);  // -> out_S[1]
    prog[1][1][8] = mov_instr(SRC_ACC, DST_EAST);   // -> out_E[2]
}

static Instr routing_relay_in() {
    return make_routing_config_instr_c<DATA_W>(RC_NONE, RC_FROM_W, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {
    return make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Programacion (identica al TB de malla directa, via noc_mesh_program)
//============================================================================
static void setup_b_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        noc_mesh_program(mesh, 0, 1, addr, relay);  // Vectorial
        noc_mesh_program(mesh, 0, 2, addr, relay);  // Escalar
    }
}

static void load_mac_program(Mesh& mesh, const Instr prog[2][2][PROG_SLOTS]) {
    for (int addr = 0; addr < PROG_LEN; addr++)
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                noc_mesh_program(mesh, MAC_CELL[i][j].row, MAC_CELL[i][j].col, addr,
                                 addr < PROG_SLOTS ? prog[i][j][addr] : nop_instr());
}

static void arm_case(Mesh& mesh) {
    noc_mesh_clear_acc(mesh);
    step_n(mesh, 1, /*rst=*/true);
    noc_mesh_program(mesh, 1, 0, /*ctx=*/0, routing_relay_in());
    noc_mesh_program(mesh, 2, 0, /*ctx=*/0, routing_relay_in());
}

//============================================================================
// Un caso de prueba: A, B (2x2) y su etiqueta.
//============================================================================
struct GemmCase {
    std::string label;
    int32_t A[2][2];
    int32_t B[2][2];
};

static void print_matrix(const char* name, const int32_t M[2][2]) {
    printf("      %s = [ %4d %4d ]\n", name, M[0][0], M[0][1]);
    printf("        %*s   [ %4d %4d ]\n", (int)strlen(name), "", M[1][0], M[1][1]);
}

static void feed_k_step(Mesh& mesh, int32_t a_row0, int32_t a_row1, int32_t b_col0, int32_t b_col1) {
    g_in_W[1] = lane0(a_row0);
    g_in_W[2] = lane0(a_row1);
    g_in_N[1] = lane0(b_col0);
    g_in_N[2] = lane0(b_col1);
    step_n(mesh, 4);
}

static bool run_case(Mesh& mesh, int case_num, const GemmCase& tc) {
    int32_t C[2][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += tc.A[i][k] * tc.B[k][j];
            C[i][j] = sum;
        }

    printf("\n============================================================\n"
           "  CASO %d -- %s\n"
           "============================================================\n", case_num, tc.label.c_str());
    print_matrix("A", tc.A);
    print_matrix("B", tc.B);
    printf("      C esperado = [ %4d %4d ]\n"
           "                   [ %4d %4d ]\n", C[0][0], C[0][1], C[1][0], C[1][1]);

    test_section((std::string("Caso ") + std::to_string(case_num) + ": k=0").c_str());
    feed_k_step(mesh, tc.A[0][0], tc.A[1][0], tc.B[0][0], tc.B[0][1]);

    test_section((std::string("Caso ") + std::to_string(case_num) + ": k=1").c_str());
    feed_k_step(mesh, tc.A[0][1], tc.A[1][1], tc.B[1][0], tc.B[1][1]);

    test_section((std::string("Caso ") + std::to_string(case_num) + ": lectura de acumuladores").c_str());
    step_n(mesh, 1);   // addr8

    noc_mesh_program(mesh, 1, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1);   // addr9

    int32_t got[2][2] = {
        { g_out_W[1][0].to_int(), g_out_E[1][0].to_int() },
        { g_out_S[1][0].to_int(), g_out_E[2][0].to_int() }
    };

    bool ok = true;
    printf("\n      C obtenido = [ %4d %4d ]\n"
           "                   [ %4d %4d ]\n\n", got[0][0], got[0][1], got[1][0], got[1][1]);
    static const char* labels[2][2] = {{"C[0][0] (out_W[1], via Routing)", "C[0][1] (out_E[1])"},
                                       {"C[1][0] (out_S[1])",              "C[1][1] (out_E[2])"}};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            test_check(ok, labels[i][j], "A,B del caso " + std::to_string(case_num), C[i][j], got[i][j]);

    printf("  Resultado del caso %d: %s\n", case_num, ok ? "PASS" : "FAIL");
    return ok;
}

static void gen_random_matrix(int32_t M[2][2], int lo, int hi) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            M[i][j] = lo + std::rand() % (hi - lo + 1);
}

int main() {
    Mesh mesh;

    printf("\n############################################################\n"
           "#  GEMM 2x2 sobre CGRA_Final_NoC_Mesh_C (layout 3x3, NoC)   #\n"
           "#  (0,0)=Memoria (0,1)=Vectorial (0,2)=Escalar              #\n"
           "#  (1,0)=Routing (1,1)=MAC       (1,2)=MAC                  #\n"
           "#  (2,0)=Routing (2,1)=MAC       (2,2)=MAC                  #\n"
           "#  Mismo programa y mismo calendario que la malla directa.  #\n"
           "############################################################\n");

    test_section("Reset");
    step_n(mesh, 1, /*rst=*/true);

    Instr prog[2][2][PROG_SLOTS];
    build_gemm_program(prog);

    test_section("Programacion inicial: relays de B (Vectorial/Escalar) + programa del bloque MAC");
    setup_b_relays(mesh);
    load_mac_program(mesh, prog);

    // Misma semilla que el TB de malla directa: los 3 casos son EXACTAMENTE
    // las mismas matrices, asi que las dos corridas son comparables linea a
    // linea.
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
        test_section((std::string("Preparar caso ") + std::to_string(c + 1) +
                      ": limpiar acumuladores + realinear pc + recargar Routing").c_str());
        arm_case(mesh);
        all_ok = run_case(mesh, c + 1, cases[c]) && all_ok;
    }

    printf("\n############################################################\n");
    if (all_ok) {
        printf("#  PASS: GEMM 2x2 correcto en los 3 casos sobre la CGRA NoC #\n");
    } else {
        printf("#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n");
    }
    printf("############################################################\n");

    return all_ok ? 0 : 1;
}
