// CGRA_Final_GEMM_C__TB.cpp
// Transliteracion a C/C++ puro de cgra_final_TB/CGRA_Final_GEMM__TB.cpp:
// GEMM 2x2 sobre la malla final 3x3 (cgra_final_c/CGRA_Final_Mesh_C.h), con
// el bloque MAC sistolico (P00=(1,1), P01=(1,2), P10=(2,1), P11=(2,2))
// EMBEBIDO en la malla mas grande -- P00 no toca ningun borde real.
//
// Caminos de entrada/salida (identicos al original; ver
// cgra_final_c/CGRA_Final_Mesh_C.h para el mapa de bordes reales):
//   A: in_W[1] -> Routing(1,0) [ctx0] -> P00 -> P01
//      in_W[2] -> Routing(2,0) [ctx0] -> P10 -> P11
//   B: in_N[1] -> Vectorial(0,1) -> P00 -> P10
//      in_N[2] -> Escalar(0,2)   -> P01 -> P11
//   C[0][0]: P00 (MOV ACC->W) -> Routing(1,0) [ctx1] -> out_W[1]
//   C[0][1]: P01 (MOV ACC->E) -> out_E[1] (borde real directo)
//   C[1][0]: P10 (MOV ACC->S) -> out_S[1] (borde real directo)
//   C[1][1]: P11 (MOV ACC->E) -> out_E[2] (borde real directo)
//
// CALENDARIO: 10 slots en vez de los 8-por-paso-de-k del original, y sin
// patron periodico. El original necesitaba margen extra porque en SystemC A
// (via Routing combinacional) y B (via Vectorial/Escalar registrado) llegaban
// DESFASADOS un ciclo, y ademas un borde recien escrito desde sc_main no se
// veia hasta el ciclo siguiente. En C ninguna de las dos cosas pasa (ver la
// nota de temporizado en CGRA_Final_Mesh_C.h): todo salto celda-a-celda cuesta
// exactamente 1 ciclo y un borde externo se ve en el mismo ciclo. Con A y B
// alineados, cada paso de k se resuelve en 4 ciclos exactos:
//
//   addr0 : Routing/Vectorial/Escalar presentan A y B en sus salidas.
//   addr1 : P00/P10 relevan A un salto al este (hacia P01/P11).
//   addr2 : P00/P01 relevan B un salto al sur (hacia P10/P11).
//   addr3 : LAS CUATRO celdas hacen su MAC(W,N)->ACC en el mismo ciclo
//           (en el original P11 tenia que esperar un slot extra).
//   addr4..7 : idem para k=1, con los bordes reescritos entre medio.
//   addr8 : las 4 celdas sacan su ACC hacia su puerto de salida.
//   addr9 : Routing(1,0), ya en ctx1, saca C[0][0] al borde real oeste.
//
// Entre casos ya no hace falta el truco de "cargar clear_acc en las 16
// direcciones y dar una vuelta completa": mesh_clear_acc() es un canal
// lateral directo (ver PE_MAC_HLS_C.h), 0 ciclos.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../cgra_final_c/CGRA_Final_Mesh_C.h"
#include "../pe_hls_c/test_util_c.h"

typedef CGRA_Final_Mesh_C Mesh;
typedef CGRA_Final_Link   Link;
typedef CGRA_Final_Instr  Instr;

static const int ROWS   = CGRA_FINAL_ROWS;
static const int COLS   = CGRA_FINAL_COLS;
static const int DATA_W = CGRA_FINAL_DATA_W;
static const int PROG_LEN = CGRA_FINAL_INSTR_MEM_SIZE;  // 16
static const int PROG_SLOTS = 10;                       // slots realmente usados

// Coordenadas (fila,columna) de las 4 celdas MAC del bloque sistolico,
// indexadas igual que GEMM_2x2_Mesh_C.h: [i][j] = P_ij.
struct Coord { int row, col; };
static const Coord MAC_CELL[2][2] = {
    {{1, 1}, {1, 2}},
    {{2, 1}, {2, 2}}
};

static Link g_in_N[COLS], g_in_S[COLS], g_in_W[ROWS], g_in_E[ROWS];

static void step_n(Mesh& mesh, int n, bool rst = false) {
    for (int i = 0; i < n; i++) cgra_final_step(mesh, rst, /*enable=*/true, g_in_N, g_in_S, g_in_W, g_in_E);
    test_count_cycles(n);
}

static Link lane0(int32_t v) { Link l; l[0] = v; return l; }

//============================================================================
// Constructores de instruccion
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

// ctx0: borde real W -> enlace interno E (entrada de A hacia el bloque MAC).
// Se agrega sel_S=FROM_W igual que en el original: solo importa para
// Routing(2,0), que SI tiene borde real S, y deja el relay observable desde
// afuera sin depender de nada rio abajo.
static Instr routing_relay_in() {
    return make_routing_config_instr_c<DATA_W>(RC_NONE, RC_FROM_W, RC_FROM_W, RC_NONE);
}
// ctx1: enlace interno E (C[0][0] de P00) -> borde real W.
static Instr routing_relay_out() {
    return make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Programacion
//============================================================================
// Relays de B: una unica instruccion replicada en las 16 direcciones, asi
// releva en TODOS los ciclos sin importar en que fase este el pc de esa celda
// (mismo criterio que el original). Sobrevive a los pulsos de rst -- rst solo
// realinea el pc, no borra instr_mem.
static void setup_b_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh_program(mesh, 0, 1, addr, relay);  // Vectorial
        mesh_program(mesh, 0, 2, addr, relay);  // Escalar
    }
}

static void load_mac_program(Mesh& mesh, const Instr prog[2][2][PROG_SLOTS]) {
    for (int addr = 0; addr < PROG_LEN; addr++)
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                mesh_program(mesh, MAC_CELL[i][j].row, MAC_CELL[i][j].col, addr,
                             addr < PROG_SLOTS ? prog[i][j][addr] : nop_instr());
}

// Deja la malla lista para arrancar un caso en la direccion 0: limpia
// acumuladores, realinea los pc con un pulso de rst y recarga los contextos
// de Routing (que ese mismo rst borra, ver Routing_Cell_HLS_C.h).
static void arm_case(Mesh& mesh) {
    mesh_clear_acc(mesh);
    step_n(mesh, 1, /*rst=*/true);
    mesh_program(mesh, 1, 0, /*ctx=*/0, routing_relay_in());
    mesh_program(mesh, 2, 0, /*ctx=*/0, routing_relay_in());
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

// Presenta la columna k de A y la fila k de B en los 4 bordes reales y corre
// los 4 ciclos del paso (relay A, relay B, MAC).
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
    step_n(mesh, 1);   // addr8: las 4 celdas sacan su ACC

    // C[0][0] es el unico que no toca un borde real: se expone conmutando
    // Routing(1,0) al ctx1 (E->W). En C el contexto queda activo en la misma
    // llamada a mesh_program(), asi que basta un ciclo mas para que el relay
    // lleve el valor al borde -- el original necesitaba 2 por el retardo de
    // bridge_instr_in -> config_bank -> route().
    mesh_program(mesh, 1, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1);   // addr9

    int32_t got[2][2] = {
        { mesh.cell<1, 0>().out_W[0].to_int(), mesh.cell<1, 2>().out_E[0].to_int() },
        { mesh.cell<2, 1>().out_S[0].to_int(), mesh.cell<2, 2>().out_E[0].to_int() }
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
           "#  GEMM 2x2 sobre CGRA_Final_Mesh_C (layout 3x3 final)      #\n"
           "#  (0,0)=Memoria (0,1)=Vectorial (0,2)=Escalar              #\n"
           "#  (1,0)=Routing (1,1)=MAC       (1,2)=MAC                  #\n"
           "#  (2,0)=Routing (2,1)=MAC       (2,2)=MAC                  #\n"
           "############################################################\n");

    test_section("Reset");
    step_n(mesh, 1, /*rst=*/true);

    Instr prog[2][2][PROG_SLOTS];
    build_gemm_program(prog);

    test_section("Programacion inicial: relays de B (Vectorial/Escalar) + programa del bloque MAC");
    setup_b_relays(mesh);
    load_mac_program(mesh, prog);

    // 3 matrices A/B de 2x2 con valores aleatorios en [-9, 9] (semilla fija
    // para que la corrida sea reproducible entre ejecuciones).
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
        printf("#  PASS: GEMM 2x2 correcto en los 3 casos sobre la CGRA final#\n");
    } else {
        printf("#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n");
    }
    printf("############################################################\n");

    return all_ok ? 0 : 1;
}
