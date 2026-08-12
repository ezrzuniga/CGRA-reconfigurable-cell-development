// CGRA_Final_SumReduce8_C__TB.cpp
// Transliteracion a C/C++ puro de cgra_final_TB/CGRA_Final_SumReduce8__TB.cpp:
// reduccion por suma de 8 enteros (TOTAL = v0+v1+...+v7) sobre la malla final
// 3x3 (cgra_final_c/CGRA_Final_Mesh_C.h).
//
// Es la unica de las cuatro aplicaciones que pone a TRABAJAR las 4 celdas MAC
// del bloque sistolico -- cada una computa al menos una suma real, no solo
// relevos. Con solo 4 bordes reales (in_W[1], in_W[2], in_N[1], in_N[2]) para
// 8 entradas, cada borde comparte 2 valores por multiplexado en el tiempo, y
// cada celda que recibe un borde directo ACUMULA sus 2 valores en un registro
// propio (MOV para el primero, ADD reg+entrada para el segundo): combina la
// tecnica temporal (acumular en el tiempo) con la espacial (sumar entre
// celdas vecinas).
//
// Arbol de reduccion (arbol binario de 3 niveles, log2(8)=3):
//                              TOTAL
//                             /      \
//                     branchA        branchB
//                    /       \       /       \
//                (v0+v1)  (v4+v5) (v2+v3)  (v6+v7)
//
// Mapeo fisico (P00 es el unico con DOS entradas reales directas -- oeste via
// Routing y norte via Vectorial -- asi que hace el doble de acumulacion
// local; P11, sin ningun borde real, combina lo que P10 y P01 le relevan):
//
//   v0,v1 -> in_W[1] -> Routing(1,0) ctx0 -> P00(oeste)  -- reg0 = v0+v1
//   v4,v5 -> in_N[1] -> Vectorial(0,1)    -> P00(norte)  -- reg1 = v4+v5
//   P00: reg2 = reg0+reg1 = branchA
//
//   v2,v3 -> in_W[2] -> Routing(2,0) ctx0 -> P10(oeste)  -- reg0 = v2+v3
//   v6,v7 -> in_N[2] -> Escalar(0,2)      -> P01(norte)  -- reg0 = v6+v7
//   P10 --(este)--> P11 ; P01 --(sur)--> P11
//   P11: reg0 = branchB = (v2+v3)+(v6+v7)
//   P11 --(norte)--> P01 --(oeste)--> P00
//   P00: TOTAL = branchA + branchB -> Routing(1,0) ctx1 -> out_W[1]
//
// Memoria (0,0) es la unica celda ociosa; las otras 8 tienen un rol real.
//
// CALENDARIO respecto del original: 12 slots en vez de 14, y los pares de
// "primer/segundo valor de cada borde" quedan a 3 ciclos de distancia en vez
// de 4-5. El motivo es el de siempre en este arbol C (ver la nota de
// temporizado en CGRA_Final_Mesh_C.h): los bordes que entran por Routing y
// los que entran por Vectorial/Escalar llegan ALINEADOS (1 ciclo los dos),
// asi que no hay que darle a los segundos un slot extra de margen.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
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

struct Coord { int row, col; };
static const Coord P00 = {1, 1};
static const Coord P01 = {1, 2};
static const Coord P10 = {2, 1};
static const Coord P11 = {2, 2};

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
static Instr mov_to_reg(ap_uint<3> src, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr mov_reg_instr(ap_uint<5> reg_a, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
// reg[reg_dst] = reg[reg_a] + src_b (reg_a y reg_dst suelen coincidir: acumulador)
static Instr accum_reg_and_dir(ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr add_reg_and_reg(ap_uint<5> reg_a, ap_uint<5> reg_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = SRC_REG; i.reg_b = reg_b;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr add_dir_and_dir_to_reg(ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = src_a; i.src_b = src_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr add_reg_and_dir_to_dir(ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<3> dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b; i.dst = dst; return i;
}
static Instr nop_instr() { return Instr(); }

// Programa espacial, 16 direcciones, sin patron repetitivo: cada borde
// escribe sus 2 valores en dos tiempos y el programa es una cadena de
// dependencias de punta a punta.
static void build_sum_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN], Instr p11[PROG_LEN]) {
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); p11[a] = nop_instr(); }

    // addr1: primer valor de cada borde (los 3 relays ya lo presentaron en addr0).
    p00[1] = mov_to_reg(SRC_WEST, 0);            // reg0 = v0
    p10[1] = mov_to_reg(SRC_WEST, 0);            // reg0 = v2
    p01[1] = mov_to_reg(SRC_NORTH, 0);           // reg0 = v6

    // addr2: P00 captura su segunda entrada directa (el borde norte sigue
    // manejado con el mismo valor, el relay Vectorial corre cada ciclo).
    p00[2] = mov_to_reg(SRC_NORTH, 1);           // reg1 = v4

    // addr3: el testbench reescribe los 4 bordes con el segundo valor; este
    // ciclo los relays lo presentan y nadie lo lee todavia (las celdas ven el
    // valor viejo, que ya quedo guardado en registro).

    // addr4: segundo valor de los bordes que se acumulan en una sola celda.
    p00[4] = accum_reg_and_dir(0, SRC_WEST, 0);  // reg0 = v0+v1
    p10[4] = accum_reg_and_dir(0, SRC_WEST, 0);  // reg0 = v2+v3
    p01[4] = accum_reg_and_dir(0, SRC_NORTH, 0); // reg0 = v6+v7

    // addr5: P00 cierra su segunda acumulacion local.
    p00[5] = accum_reg_and_dir(1, SRC_NORTH, 1); // reg1 = v4+v5

    // addr6: combina localmente (P00) y releva hacia P11 (P10, P01).
    p00[6] = add_reg_and_reg(0, 1, 2);           // reg2 = branchA = (v0+v1)+(v4+v5)
    p10[6] = mov_reg_instr(0, DST_EAST);         // v2+v3 -> P11 (oeste de P11)
    p01[6] = mov_reg_instr(0, DST_SOUTH);        // v6+v7 -> P11 (norte de P11)

    // addr7: P11 combina lo que le llego de P10 (oeste) y P01 (norte).
    p11[7] = add_dir_and_dir_to_reg(SRC_WEST, SRC_NORTH, 0);  // reg0 = branchB

    // addr8: P11 releva branchB hacia P01 (su vecino norte).
    p11[8] = mov_reg_instr(0, DST_NORTH);

    // addr9: P01 reenvia branchB (que le llego por el sur) hacia P00.
    p01[9] = mov_instr(SRC_SOUTH, DST_WEST);

    // addr10: P00 suma su rama local con branchB (llegada por el este) y saca
    // el TOTAL por el oeste -> Routing(1,0) ctx1 -> borde real.
    p00[10] = add_reg_and_dir_to_dir(2, SRC_EAST, DST_WEST);
}

static Instr routing_relay_in()  {  // ctx0: borde real W -> enlace interno E
    return make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {  // ctx1: enlace interno E -> borde real W
    return make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Programacion (0 ciclos: mesh_program escribe instr_mem/config_bank directo)
//============================================================================
static void load_sum_program(Mesh& mesh, const Instr p00[PROG_LEN], const Instr p01[PROG_LEN],
                              const Instr p10[PROG_LEN], const Instr p11[PROG_LEN]) {
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh_program(mesh, P00.row, P00.col, addr, p00[addr]);
        mesh_program(mesh, P01.row, P01.col, addr, p01[addr]);
        mesh_program(mesh, P10.row, P10.col, addr, p10[addr]);
        mesh_program(mesh, P11.row, P11.col, addr, p11[addr]);
    }
}

// Relays de borde: una unica instruccion replicada en las 16 direcciones, asi
// releva en todos los ciclos sin importar la fase del pc.
static void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh_program(mesh, 0, 1, addr, relay);  // Vectorial: v4/v5 -> P00
        mesh_program(mesh, 0, 2, addr, relay);  // Escalar:   v6/v7 -> P01
    }
}

// Realinea los pc en 0 y recarga los contextos de Routing que el rst borra.
static void arm_case(Mesh& mesh) {
    step_n(mesh, 1, /*rst=*/true);
    mesh_program(mesh, P00.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(1,0): v0/v1 -> P00
    mesh_program(mesh, P10.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(2,0): v2/v3 -> P10
}

//============================================================================
// Un caso de prueba: 8 enteros y su etiqueta.
//============================================================================
struct SumCase {
    std::string label;
    int32_t v[8];
};

static bool run_case(Mesh& mesh, int case_num, const SumCase& tc) {
    int32_t expected = 0;
    for (int i = 0; i < 8; i++) expected += tc.v[i];

    printf("\n============================================================\n"
           "  CASO %d -- %s\n"
           "============================================================\n"
           "      v = [", case_num, tc.label.c_str());
    for (int i = 0; i < 8; i++) printf("%d%s", tc.v[i], i < 7 ? ", " : "");
    printf("]\n      TOTAL esperado = %d\n", expected);

    test_section((std::string("Caso ") + std::to_string(case_num) + ": ejecucion").c_str());

    // t1: primer valor de cada borde.
    g_in_W[P00.row] = lane0(tc.v[0]);
    g_in_W[P10.row] = lane0(tc.v[2]);
    g_in_N[1]       = lane0(tc.v[4]);
    g_in_N[2]       = lane0(tc.v[6]);
    step_n(mesh, 3);   // addr0..2: relays + v0/v2/v6 y v4 guardados en registro

    // t2: segundo valor de cada borde -- seguro de escribir ahora, los
    // primeros ya se acumularon en registros propios de cada celda.
    g_in_W[P00.row] = lane0(tc.v[1]);
    g_in_W[P10.row] = lane0(tc.v[3]);
    g_in_N[1]       = lane0(tc.v[5]);
    g_in_N[2]       = lane0(tc.v[7]);
    step_n(mesh, 4);   // addr3: relays presentan t2 | addr4: v0+v1,v2+v3,v6+v7 | addr5: v4+v5 | addr6: branchA + relevos
    step_n(mesh, 3);   // addr7: branchB en P11 | addr8: branchB -> P01 | addr9: branchB -> P00
    step_n(mesh, 1);   // addr10: TOTAL = branchA + branchB -> puerto oeste de P00

    // Conmutar Routing(1,0) a ctx1 (E->W) para sacar el TOTAL al borde real.
    mesh_program(mesh, P00.row, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1);

    int32_t got = mesh.cell<1, 0>().out_W[0].to_int();

    bool pass = (got == expected);
    printf("      TOTAL obtenido  = %d\n\n"
           "%s TOTAL  esperado=%d  obtenido=%d\n"
           "  Resultado del caso %d: %s\n",
           got, pass ? "PASS" : "FAIL", expected, got, case_num, pass ? "PASS" : "FAIL");
    return pass;
}

static void gen_random_case(int32_t v[8], int lo, int hi) {
    for (int i = 0; i < 8; i++) v[i] = lo + std::rand() % (hi - lo + 1);
}

int main() {
    Mesh mesh;

    printf("\n############################################################\n"
           "#  Reduccion de suma (8 enteros) sobre CGRA_Final_Mesh_C     #\n"
           "#  (0,0)=Memoria (sin uso) (0,1)=Vectorial   (0,2)=Escalar   #\n"
           "#  (1,0)=Routing  (1,1)=MAC(P00, hub)  (1,2)=MAC(P01)        #\n"
           "#  (2,0)=Routing  (2,1)=MAC(P10)       (2,2)=MAC(P11)        #\n"
           "#  Las 4 celdas MAC computan una suma real (no solo relevos) #\n"
           "############################################################\n");

    test_section("Reset");
    step_n(mesh, 1, /*rst=*/true);

    Instr p00[PROG_LEN], p01[PROG_LEN], p10[PROG_LEN], p11[PROG_LEN];
    build_sum_program(p00, p01, p10, p11);

    test_section("Programacion inicial: relays de Vectorial/Escalar + arbol de suma");
    setup_relays(mesh);
    load_sum_program(mesh, p00, p01, p10, p11);

    std::srand(20260810);
    SumCase cases[3];
    cases[0].label = "unos y ceros (arbol facil de seguir a mano)";
    for (int i = 0; i < 8; i++) cases[0].v[i] = 1;
    cases[1].label = "potencias de dos (una contribucion por rama es identificable)";
    for (int i = 0; i < 8; i++) cases[1].v[i] = 1 << i;
    cases[2].label = "enteros aleatorios con signo";
    gen_random_case(cases[2].v, -20, 20);

    bool all_ok = true;
    for (int c = 0; c < 3; c++) {
        test_section((std::string("Preparar caso ") + std::to_string(c + 1) +
                      ": realinear pc + recargar Routing").c_str());
        arm_case(mesh);
        all_ok = run_case(mesh, c + 1, cases[c]) && all_ok;
    }

    printf("\n############################################################\n");
    if (all_ok) {
        printf("#  PASS: reduccion de suma correcta en los 3 casos          #\n");
    } else {
        printf("#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n");
    }
    printf("############################################################\n");

    return all_ok ? 0 : 1;
}
