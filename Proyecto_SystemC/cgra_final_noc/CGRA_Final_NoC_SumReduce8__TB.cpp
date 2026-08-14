// CGRA_Final_NoC_SumReduce8__TB.cpp
// Puerto directo de cgra_final_TB/CGRA_Final_SumReduce8__TB.cpp a
// CGRA_Final_NoC_Mesh: MISMO arbol de reduccion, MISMO programa espacial
// (16 direcciones, cadena de dependencias sin patron repetitivo), MISMOS
// margenes de ciclos -- igual que CGRA_Final_NoC_GEMM__TB.cpp, la unica
// diferencia real con el original es el include y los typedefs (Mesh/Link/
// Instr/constantes _NOC_).
//
// Por que esta es la prueba de equivalencia mas exigente de las tres: es la
// UNICA de las 3 aplicaciones (Mesh smoke test, GEMM, esta) donde las 4
// celdas MAC computan trabajo real y ademas hay 2 celdas con DOS entradas de
// borde multiplexadas en el tiempo (P00 acumula v0/v1 por el oeste Y v4/v5
// por el norte) -- el patron de trafico por la fabrica NoC es mas denso e
// irregular que el de GEMM (que reusa el mismo programa 4 veces en bucle).
// Si esto pasa sin tocar un solo ciclo del programa original, es la prueba
// mas fuerte de que NoC_Router (combinacional, sin latencia extra) es un
// reemplazo de interconexion cycle-accurate identico, no solo funcionalmente
// equivalente.
//
// Ver cgra_final_TB/CGRA_Final_SumReduce8__TB.md para el arbol de reduccion
// completo, el mapeo fisico celda por celda y la tabla de instrucciones --
// no se repite aca.

#include <systemc.h>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>
#include <array>
#include "CGRA_Final_NoC_Mesh.h"
#include "../pe_hls/test_util.h"

typedef CGRA_Final_NoC_Mesh Mesh;
typedef CGRA_Final_NoC_Link Link;
typedef CGRA_Final_NoC_Instr Instr;

static const int PROG_LEN = CGRA_FINAL_NOC_INSTR_MEM_SIZE;  // 16

struct Coord { int row, col; };
static const Coord P00 = {1, 1};
static const Coord P01 = {1, 2};
static const Coord P10 = {2, 1};
static const Coord P11 = {2, 2};

//============================================================================
// Constructores de instruccion
//============================================================================
static Instr mov_instr(sc_uint<3> src, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
static Instr mov_to_reg(sc_uint<3> src, sc_uint<5> reg_dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr mov_reg_instr(sc_uint<5> reg_a, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
static Instr accum_reg_and_dir(sc_uint<5> reg_a, sc_uint<3> src_b, sc_uint<5> reg_dst) {
    // reg[reg_dst] = reg[reg_a] + src_b  (reg_a y reg_dst suelen ser el mismo indice: acumulador)
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr add_reg_and_reg(sc_uint<5> reg_a, sc_uint<5> reg_b, sc_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = SRC_REG; i.reg_b = reg_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr add_dir_and_dir_to_reg(sc_uint<3> src_a, sc_uint<3> src_b, sc_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = src_a; i.src_b = src_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr add_reg_and_dir_to_dir(sc_uint<5> reg_a, sc_uint<3> src_b, sc_uint<3> dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b; i.dst = dst; return i;
}
static Instr nop_instr() { return Instr(); }

// Programa espacial, 16 direcciones, sin patron repetitivo -- identico al
// original (cgra_final_TB/CGRA_Final_SumReduce8__TB.cpp).
static void build_sum_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN], Instr p11[PROG_LEN]) {
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); p11[a] = nop_instr(); }

    // addr1: primer valor de cada borde directo, ya estable.
    p00[1] = mov_to_reg(SRC_WEST, 0);                        // reg0 = v0
    p10[1] = mov_to_reg(SRC_WEST, 0);                        // reg0 = v2

    // addr2: bordes via relevo registrado (Vectorial/Escalar), listos un
    // ciclo mas tarde que los bordes directos de Routing.
    p00[2] = mov_to_reg(SRC_NORTH, 1);                       // reg1 = v4
    p01[2] = mov_to_reg(SRC_NORTH, 0);                       // reg0 = v6

    // addr4: segundo valor de los bordes directos, ya reescrito y estable.
    p00[4] = accum_reg_and_dir(0, SRC_WEST, 0);              // reg0 = v0+v1
    p10[4] = accum_reg_and_dir(0, SRC_WEST, 0);              // reg0 = v2+v3

    // addr5: segundo valor via Vectorial/Escalar.
    p00[5] = accum_reg_and_dir(1, SRC_NORTH, 1);             // reg1 = v4+v5
    p01[5] = accum_reg_and_dir(0, SRC_NORTH, 0);             // reg0 = v6+v7

    // addr6: combina localmente (P00) y releva hacia P11 (P10, P01).
    p00[6] = add_reg_and_reg(0, 1, 2);                       // reg2 = branchA = (v0+v1)+(v4+v5)
    p10[6] = mov_reg_instr(0, DST_EAST);                     // v2+v3 -> P11 (oeste de P11)
    p01[6] = mov_reg_instr(0, DST_SOUTH);                    // v6+v7 -> P11 (norte de P11)

    // addr8: P11 combina lo que le llego de P10 (oeste) y P01 (norte).
    p11[8] = add_dir_and_dir_to_reg(SRC_WEST, SRC_NORTH, 0); // reg0 = branchB = (v2+v3)+(v6+v7)

    // addr9: P11 releva branchB hacia P01 (su vecino norte).
    p11[9] = mov_reg_instr(0, DST_NORTH);

    // addr11: P01 reenvia branchB (que le llego por el sur) hacia P00.
    p01[11] = mov_instr(SRC_SOUTH, DST_WEST);

    // addr13: P00 suma su rama local con branchB (llegada por el este) y
    // saca el TOTAL por el oeste -> Routing(1,0) ctx1 -> borde real.
    p00[13] = add_reg_and_dir_to_dir(2, SRC_EAST, DST_WEST);
}

static Instr routing_relay_in()  {  // ctx0: borde real W -> enlace interno E
    return make_routing_config_instr_hls<CGRA_FINAL_NOC_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {  // ctx1: enlace interno E -> borde real W
    return make_routing_config_instr_hls<CGRA_FINAL_NOC_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Carga de programas -- identico al original (dos vueltas: una escribe
// instr_mem, la vuelta "margen" que sigue es la primera donde el programa ya
// asentado se ejecuta de verdad).
//============================================================================
static void load_sum_program(Mesh& mesh, sc_signal<bool>& rst,
                              const Instr p00[PROG_LEN], const Instr p01[PROG_LEN],
                              const Instr p10[PROG_LEN], const Instr p11[PROG_LEN]) {
    rst.write(true);
    advance_cycles(1);
    rst.write(false);

    for (int addr = 0; addr < PROG_LEN; addr++) {
        if (addr == 0) {
            mesh.load_instr(P00.row, 0, 0, routing_relay_in());   // Routing(1,0) ctx0: v0/v1 -> P00
            mesh.load_instr(P10.row, 0, 0, routing_relay_in());   // Routing(2,0) ctx0: v2/v3 -> P10
        }
        mesh.load_instr(P00.row, P00.col, addr, p00[addr]);
        mesh.load_instr(P01.row, P01.col, addr, p01[addr]);
        mesh.load_instr(P10.row, P10.col, addr, p10[addr]);
        mesh.load_instr(P11.row, P11.col, addr, p11[addr]);
        advance_cycles(1);
    }
    mesh.clear_instr(P00.row, P00.col);
    mesh.clear_instr(P01.row, P01.col);
    mesh.clear_instr(P10.row, P10.col);
    mesh.clear_instr(P11.row, P11.col);
    mesh.clear_instr(P00.row, 0);
    mesh.clear_instr(P10.row, 0);
}

static void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh.load_instr(0, 1, addr, relay);  // Vectorial: v4/v5 -> P00
        mesh.load_instr(0, 2, addr, relay);  // Escalar:   v6/v7 -> P01
        advance_cycles(1);
    }
    mesh.clear_instr(0, 1);
    mesh.clear_instr(0, 2);
}

//============================================================================
// Un caso de prueba: 8 enteros y su etiqueta.
//============================================================================
struct SumCase {
    std::string label;
    std::array<int32_t, 8> v;
};

static bool run_case(Mesh& mesh, sc_signal<bool>& rst,
                      sc_signal<Link>* in_N, sc_signal<Link>* in_W, sc_signal<Link>* out_W,
                      int case_num, const SumCase& tc) {
    int32_t expected = 0;
    for (int i = 0; i < 8; i++) expected += tc.v[i];

    cout << "\n============================================================\n"
         << "  CASO " << case_num << " -- " << tc.label << " (sobre CGRA_Final_NoC_Mesh)\n"
         << "============================================================\n"
         << "      v = [";
    for (int i = 0; i < 8; i++) cout << tc.v[i] << (i < 7 ? ", " : "");
    cout << "]\n      TOTAL esperado = " << expected << "\n";

    // t1: primer valor de cada borde.
    in_W[P00.row].write(Link({tc.v[0]}));
    in_W[P10.row].write(Link({tc.v[2]}));
    in_N[1].write(Link({tc.v[4]}));
    in_N[2].write(Link({tc.v[6]}));

    test_section((std::string("Caso ") + std::to_string(case_num) + ": ejecucion").c_str());
    advance_cycles(2);   // addr0..1: settle + v0/v2 acumulados
    // t2: segundo valor de cada borde -- seguro de escribir ahora, los
    // primeros valores ya se acumularon en registros propios de cada celda.
    in_W[P00.row].write(Link({tc.v[1]}));
    in_W[P10.row].write(Link({tc.v[3]}));
    in_N[1].write(Link({tc.v[5]}));
    in_N[2].write(Link({tc.v[7]}));
    advance_cycles(2);   // addr2..3: v4/v6 acumulados (via Vectorial/Escalar) + settle de t2
    advance_cycles(1);   // addr4: v0+v1, v2+v3
    advance_cycles(1);   // addr5: v4+v5, v6+v7
    advance_cycles(1);   // addr6: branchA en P00; P10/P01 relevan hacia P11
    advance_cycles(2);   // addr7..8: settle + branchB en P11
    advance_cycles(1);   // addr9: P11 releva branchB hacia P01
    advance_cycles(1);   // addr10: settle
    advance_cycles(1);   // addr11: P01 reenvia branchB hacia P00
    // Routing(1,0) ctx1, con 2 ciclos de margen antes de addr13 (mismo motivo
    // que en CGRA_Final_NoC_GEMM__TB: un config recien cargado no se refleja
    // en el ciclo inmediato siguiente).
    mesh.load_instr(P00.row, 0, 1, routing_relay_out());
    advance_cycles(1);   // addr12: settle
    advance_cycles(1);   // addr13: TOTAL = branchA + branchB -> Routing ctx1

    int32_t got = (int32_t)out_W[P00.row].read()[0];
    mesh.clear_instr(P00.row, 0);

    bool pass = (got == expected);
    cout << "      TOTAL obtenido  = " << got << "\n\n"
         << (pass ? "PASS " : "FAIL ") << "TOTAL  esperado=" << expected << "  obtenido=" << got << "\n"
         << "  Resultado del caso " << case_num << ": " << (pass ? "PASS" : "FAIL") << "\n";
    return pass;
}

static void gen_random_case(std::array<int32_t, 8>& v, int lo, int hi) {
    for (int i = 0; i < 8; i++) v[i] = lo + std::rand() % (hi - lo + 1);
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[CGRA_FINAL_NOC_COLS], out_N[CGRA_FINAL_NOC_COLS];
    sc_signal<Link> in_S[CGRA_FINAL_NOC_COLS], out_S[CGRA_FINAL_NOC_COLS];
    sc_signal<Link> in_W[CGRA_FINAL_NOC_ROWS], out_W[CGRA_FINAL_NOC_ROWS];
    sc_signal<Link> in_E[CGRA_FINAL_NOC_ROWS], out_E[CGRA_FINAL_NOC_ROWS];

    Mesh mesh("mesh");
    mesh.clk(clk);
    mesh.rst(rst);
    mesh.enable(enable);
    for (int c = 0; c < CGRA_FINAL_NOC_COLS; c++) {
        mesh.in_N[c](in_N[c]); mesh.out_N[c](out_N[c]);
        mesh.in_S[c](in_S[c]); mesh.out_S[c](out_S[c]);
    }
    for (int r = 0; r < CGRA_FINAL_NOC_ROWS; r++) {
        mesh.in_W[r](in_W[r]); mesh.out_W[r](out_W[r]);
        mesh.in_E[r](in_E[r]); mesh.out_E[r](out_E[r]);
    }

    cout << "\n############################################################\n"
         << "#  Reduccion de suma (8 enteros) sobre CGRA_Final_NoC_Mesh   #\n"
         << "#  (interconexion NoC: routers en vez de wires directos)     #\n"
         << "#  (0,0)=Memoria (sin uso) (0,1)=Vectorial   (0,2)=Escalar   #\n"
         << "#  (1,0)=Routing  (1,1)=MAC(P00, hub)  (1,2)=MAC(P01)        #\n"
         << "#  (2,0)=Routing  (2,1)=MAC(P10)       (2,2)=MAC(P11)        #\n"
         << "#  Las 4 celdas MAC computan una suma real (no solo relevos) #\n"
         << "############################################################\n";

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < CGRA_FINAL_NOC_COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < CGRA_FINAL_NOC_ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    advance_cycles(2);
    rst.write(false);
    enable.write(true);

    Instr p00[PROG_LEN], p01[PROG_LEN], p10[PROG_LEN], p11[PROG_LEN];
    build_sum_program(p00, p01, p10, p11);

    test_section("Programacion inicial: relays de Vectorial/Escalar + arbol de suma");
    setup_relays(mesh);
    load_sum_program(mesh, rst, p00, p01, p10, p11);
    advance_cycles(PROG_LEN);  // margen: primera vuelta con el programa ya asentado

    std::srand(20260810);
    std::vector<SumCase> cases(3);
    cases[0].label = "unos y ceros (arbol facil de seguir a mano)";
    cases[0].v = {1, 1, 1, 1, 1, 1, 1, 1};
    cases[1].label = "potencias de dos (una contribucion por rama es identificable)";
    cases[1].v = {1, 2, 4, 8, 16, 32, 64, 128};
    cases[2].label = "enteros aleatorios con signo";
    gen_random_case(cases[2].v, -20, 20);

    bool all_ok = true;
    for (size_t c = 0; c < cases.size(); c++) {
        bool ok = run_case(mesh, rst, in_N, in_W, out_W, (int)c + 1, cases[c]);
        all_ok = all_ok && ok;
        if (c + 1 < cases.size()) {
            test_section("Entre casos: recargar programa");
            load_sum_program(mesh, rst, p00, p01, p10, p11);
            advance_cycles(PROG_LEN);
        }
    }

    cout << "\n############################################################\n";
    if (all_ok) {
        cout << "#  PASS: reduccion de suma correcta en los 3 casos (NoC)    #\n";
    } else {
        cout << "#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n";
    }
    cout << "############################################################\n";

    return all_ok ? 0 : 1;
}
