// CGRA_Final_SumReduce8__TB.cpp
// Reduccion por suma de 8 enteros sobre la malla final 3x3
// (cgra_final/CGRA_Final_Mesh.h): TOTAL = v0+v1+...+v7. A diferencia de
// CGRA_Final_GEMM__TB (2 celdas MAC activas) y CGRA_Final_FFT4__TB (2 celdas
// MAC activas + 2 de relevo puro), esta es la unica de las tres que pone a
// TRABAJAR las 4 celdas MAC del bloque sistolico -- cada una computa al
// menos una suma real, no solo relevos.
//
// Con solo 4 bordes reales (in_W[1], in_W[2], in_N[1], in_N[2]) para 8
// entradas, cada borde comparte 2 valores por multiplexado en el tiempo
// (mismo truco que x2/x3 en CGRA_Final_FFT4__TB), y cada celda que recibe un
// borde directo ACUMULA sus 2 valores en un registro propio (MOV para el
// primero, ADD reg+entrada para el segundo) -- combina la tecnica temporal
// (acumular en el tiempo) de PE_MAC_HLS::acc con la tecnica espacial (sumar
// entre celdas vecinas) que ya usaban GEMM y FFT.
//
// Por que Escalar (0,2) SI participa aca (a diferencia de FFT): la lane 0
// unica que preserva PE_Scalar_Cell_HLS (ver comentario en
// CGRA_Final_FFT4__TB.cpp) es exactamente lo que hace falta para un entero
// escalar -- no hay parte imaginaria que perder.
//
// Arbol de reduccion (arbol binario de 3 niveles, log2(8)=3):
//                              TOTAL
//                             /      \
//                     branchA        branchB
//                    /       \       /       \
//                (v0+v1)  (v4+v5) (v2+v3)  (v6+v7)
//
// Mapeo fisico (P00 es el unico borde con DOS entradas reales directas --
// oeste via Routing y norte via Vectorial -- asi que hace el doble de
// acumulacion local; P10/P01 acumulan su unico borde directo; P11, sin
// ningun borde real, combina lo que P10 y P01 le relevan):
//
//   v0,v1 -> in_W[1] -> Routing(1,0) ctx0 -> P00(oeste)  -- reg0 = v0+v1
//   v4,v5 -> in_N[1] -> Vectorial(0,1)    -> P00(norte)  -- reg1 = v4+v5
//   P00: reg2 = reg0+reg1 = branchA = v0+v1+v4+v5
//
//   v2,v3 -> in_W[2] -> Routing(2,0) ctx0 -> P10(oeste)  -- reg0 = v2+v3
//   v6,v7 -> in_N[2] -> Escalar(0,2)      -> P01(norte)  -- reg0 = v6+v7
//   P10 --v2+v3(este)--> P11   P01 --v6+v7(sur)--> P11
//   P11: reg0 = branchB = v2+v3+v6+v7
//   P11 --branchB(norte)--> P01 --branchB(oeste)--> P00
//   P00: TOTAL = branchA + branchB  -> Routing(1,0) ctx1 -> out_W[1]
//
// Memoria (0,0) sigue sin participar (ninguna reduccion de este tamano
// necesita SRAM) -- es la unica celda ociosa; las otras 8 (Vectorial,
// Escalar, 2 Routing, 4 MAC) tienen todas un rol real esta vez.

#include <systemc.h>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>
#include <array>
#include "../cgra_final/CGRA_Final_Mesh.h"
#include "../pe_hls/test_util.h"

typedef CGRA_Final_Mesh Mesh;
typedef CGRA_Final_Link Link;
typedef CGRA_Final_Instr Instr;

static const int PROG_LEN = CGRA_FINAL_INSTR_MEM_SIZE;  // 16

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

// Programa espacial, 16 direcciones, sin patron repetitivo (como
// CGRA_Final_FFT4__TB, no como el k-loop periodico de CGRA_Final_GEMM__TB):
// cada borde escribe sus 2 valores en dos tiempos y el programa es una
// cadena de dependencias de punta a punta.
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
    return make_routing_config_instr_hls<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {  // ctx1: enlace interno E -> borde real W
    return make_routing_config_instr_hls<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Carga de programas -- mismo patron de dos vueltas que los otros dos TB
// finales (una vuelta de 16 ciclos escribe instr_mem, la vuelta "margen" que
// sigue es la primera donde el programa ya asentado se ejecuta de verdad).
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
         << "  CASO " << case_num << " -- " << tc.label << "\n"
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
    // que en CGRA_Final_GEMM__TB / CGRA_Final_FFT4__TB: un config recien
    // cargado no se refleja en el ciclo inmediato siguiente).
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
    sc_signal<Link> in_N[CGRA_FINAL_COLS], out_N[CGRA_FINAL_COLS];
    sc_signal<Link> in_S[CGRA_FINAL_COLS], out_S[CGRA_FINAL_COLS];
    sc_signal<Link> in_W[CGRA_FINAL_ROWS], out_W[CGRA_FINAL_ROWS];
    sc_signal<Link> in_E[CGRA_FINAL_ROWS], out_E[CGRA_FINAL_ROWS];

    Mesh mesh("mesh");
    mesh.clk(clk);
    mesh.rst(rst);
    mesh.enable(enable);
    for (int c = 0; c < CGRA_FINAL_COLS; c++) {
        mesh.in_N[c](in_N[c]); mesh.out_N[c](out_N[c]);
        mesh.in_S[c](in_S[c]); mesh.out_S[c](out_S[c]);
    }
    for (int r = 0; r < CGRA_FINAL_ROWS; r++) {
        mesh.in_W[r](in_W[r]); mesh.out_W[r](out_W[r]);
        mesh.in_E[r](in_E[r]); mesh.out_E[r](out_E[r]);
    }

    cout << "\n############################################################\n"
         << "#  Reduccion de suma (8 enteros) sobre CGRA_Final_Mesh       #\n"
         << "#  (0,0)=Memoria (sin uso) (0,1)=Vectorial   (0,2)=Escalar   #\n"
         << "#  (1,0)=Routing  (1,1)=MAC(P00, hub)  (1,2)=MAC(P01)        #\n"
         << "#  (2,0)=Routing  (2,1)=MAC(P10)       (2,2)=MAC(P11)        #\n"
         << "#  Las 4 celdas MAC computan una suma real (no solo relevos) #\n"
         << "############################################################\n";

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < CGRA_FINAL_COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < CGRA_FINAL_ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
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
        cout << "#  PASS: reduccion de suma correcta en los 3 casos          #\n";
    } else {
        cout << "#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n";
    }
    cout << "############################################################\n";

    return all_ok ? 0 : 1;
}
