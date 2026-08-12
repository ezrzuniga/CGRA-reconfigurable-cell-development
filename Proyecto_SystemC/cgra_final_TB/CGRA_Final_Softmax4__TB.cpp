// CGRA_Final_Softmax4__TB.cpp
// Softmax de 4 elementos sobre la malla final 3x3 (cgra_final/CGRA_Final_Mesh.h).
//
// Esta ISA no tiene exponencial ni division (ver pe_isa.h: ADD/SUB/AND/OR/
// XOR/shifts/comparaciones/MUL/MAC, nada mas), asi que un softmax "exacto"
// de punto flotante no es posible en la malla. Este testbench reproduce lo
// que hace un acelerador real: separa la parte PARALELIZABLE (exponencial
// por elemento + reduccion de la suma, ambas O(N) trabajo independiente por
// elemento) de la parte ESCALAR (una unica division por todo el vector, que
// no se beneficia de paralelizarse porque ocurre una sola vez). La malla
// hace la primera parte; CGRA_Final_Softmax4__TB::run_case hace la segunda
// en C++, fuera del array -- exactamente la misma frontera de diseno que
// tienen los aceleradores reales para softmax en hardware.
//
// exp2 exacto via shift (no una aproximacion con error): en vez de e^x,
// esta malla computa 2^x escalado -- un softmax de BASE 2 en vez de base e.
// softmax_2(x) = 2^x / sum(2^x) es exactamente igual a softmax_e evaluado
// en logits pre-escalados por ln(2) (2^x = e^(x*ln2)), asi que sigue siendo
// un softmax legitimo (mismo orden relativo, misma forma), solo con otra
// temperatura -- y es EXACTO, no aproximado, porque 2^x de un entero x es
// un shift, no una serie truncada. Para poder representar 2^x con enteros
// aun cuando x sea negativo, todo se escala por una constante K=9 fija:
//   EXP2(x) = 1 << (x+9)
// valido sin overflow ni resultado negativo para x en [-9, 9] (el mismo
// rango que usan los demas TB de este arbol), shift computable en una unica
// instruccion OP_SLL con el shift-amount armado por un ADD previo.
//
// Arbol de reduccion (2 niveles, log2(4)=2 -- mismo espiritu que el arbol de
// 3 niveles de CGRA_Final_SumReduce8__TB pero mas chico, porque aca solo
// hay 4 entradas: cada una de las 4 usa un borde real distinto, sin
// necesitar multiplexado en el tiempo como el de 8 entradas):
//
//   x0 -> in_W[1] -> Routing(1,0) ctx0 -> P00(oeste)  -- e0 = EXP2(x0)
//   x1 -> in_N[1] -> Vectorial(0,1)    -> P00(norte)  -- e1 = EXP2(x1)
//   x2 -> in_W[2] -> Routing(2,0) ctx0 -> P10(oeste)  -- e2 = EXP2(x2)
//   x3 -> in_N[2] -> Escalar(0,2)      -> P01(norte)  -- e3 = EXP2(x3)
//
//   P00 ya tiene e0,e1 localmente; P10 --e2(norte)--> P00(sur);
//   P01 --e3(oeste)--> P00(este) -- P00 es vecino directo de ambos, asi que
//   no hace falta una celda P11 combinadora como en la reduccion de 8.
//
//   P00: SUM = e0+e1+e2+e3 -> Routing(1,0) ctx1 -> out_W[1]
//   P00: e0 (spot-check)   -> mismo puerto, un tiempo despues
//
// Memoria (0,0) sigue sin participar.

#include <systemc.h>
#include <cstdint>
#include <cstdlib>
#include <cmath>
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
static const int32_t EXP2_SHIFT_BIAS = 9;  // EXP2(x) = 1 << (x + 9), valido para x en [-9, 9]

struct Coord { int row, col; };
static const Coord P00 = {1, 1};
static const Coord P01 = {1, 2};
static const Coord P10 = {2, 1};

//============================================================================
// Constructores de instruccion
//============================================================================
static Instr mov_instr(sc_uint<3> src, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
static Instr mov_reg_instr(sc_uint<5> reg_a, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
static Instr add_imm_to_reg(sc_uint<3> src, int32_t imm, sc_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = src; i.src_b = SRC_IMM; i.imm = imm; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr shl_one_by_reg(sc_uint<5> shamt_reg, sc_uint<5> reg_dst) {
    // reg[reg_dst] = 1 << reg[shamt_reg]  (EXP2 exacto via shift)
    Instr i; i.opcode = OP_SLL; i.src_a = SRC_IMM; i.imm = 1; i.src_b = SRC_REG; i.reg_b = shamt_reg; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr accum_reg_and_reg(sc_uint<5> reg_a, sc_uint<5> reg_b, sc_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = SRC_REG; i.reg_b = reg_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr accum_reg_and_dir(sc_uint<5> reg_a, sc_uint<3> src_b, sc_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr nop_instr() { return Instr(); }

// Registros de P00: 0=tmp0/e0, 1=tmp1/e1, 2=SUM (se reusan tmp->e en el
// mismo indice porque para cuando se calcula e_i ya no hace falta tmp_i).
static void build_softmax_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN]) {
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); }

    // addr1: bordes directos via Routing (0-cyc + 1 settle), ya estables.
    p00[1] = add_imm_to_reg(SRC_WEST, EXP2_SHIFT_BIAS, 0);    // tmp0 = x0+9
    p10[1] = add_imm_to_reg(SRC_WEST, EXP2_SHIFT_BIAS, 0);    // tmp2 = x2+9

    // addr2: EXP2 de los directos; tmp3 (Escalar, listo un ciclo mas tarde).
    p00[2] = shl_one_by_reg(0, 0);                            // e0 = 1<<tmp0
    p10[2] = shl_one_by_reg(0, 0);                            // e2 = 1<<tmp2
    p01[2] = add_imm_to_reg(SRC_NORTH, EXP2_SHIFT_BIAS, 0);   // tmp3 = x3+9

    // addr3: x1 (Vectorial) ya estable; EXP2 de x3.
    p00[3] = add_imm_to_reg(SRC_NORTH, EXP2_SHIFT_BIAS, 1);   // tmp1 = x1+9
    p01[3] = shl_one_by_reg(0, 0);                            // e3 = 1<<tmp3

    // addr4: EXP2 de x1.
    p00[4] = shl_one_by_reg(1, 1);                            // e1 = 1<<tmp1

    // addr5: P00 combina localmente e0+e1; P10/P01 relevan e2/e3 hacia P00.
    p00[5] = accum_reg_and_reg(0, 1, 2);                      // reg2 = e0+e1
    p10[5] = mov_reg_instr(0, DST_NORTH);                     // e2 -> P00 (sur de P00)
    p01[5] = mov_reg_instr(0, DST_WEST);                      // e3 -> P00 (este de P00)

    // addr7/8: P00 termina la suma con los dos valores relevados.
    p00[7] = accum_reg_and_dir(2, SRC_SOUTH, 2);              // reg2 += e2
    p00[8] = accum_reg_and_dir(2, SRC_EAST, 2);               // reg2 += e3 = SUM

    // addr10/11: saca SUM y despues e0 (spot-check) por el mismo puerto,
    // en dos tiempos distintos -- mismo patron que C[0][0]/C[1][0] en
    // CGRA_Final_GEMM__TB.
    p00[10] = mov_reg_instr(2, DST_WEST);                     // SUM -> Routing ctx1 -> out_W[1]
    p00[11] = mov_reg_instr(0, DST_WEST);                     // e0  -> mismo puerto
}

static Instr routing_relay_in()  {  // ctx0: borde real W -> enlace interno E
    return make_routing_config_instr_hls<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {  // ctx1: enlace interno E -> borde real W
    return make_routing_config_instr_hls<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Carga de programas -- mismo patron de dos vueltas que los demas TB finales.
//============================================================================
static void load_softmax_program(Mesh& mesh, sc_signal<bool>& rst,
                                  const Instr p00[PROG_LEN], const Instr p01[PROG_LEN], const Instr p10[PROG_LEN]) {
    rst.write(true);
    advance_cycles(1);
    rst.write(false);

    for (int addr = 0; addr < PROG_LEN; addr++) {
        if (addr == 0) {
            mesh.load_instr(P00.row, 0, 0, routing_relay_in());   // Routing(1,0) ctx0: x0 -> P00
            mesh.load_instr(P10.row, 0, 0, routing_relay_in());   // Routing(2,0) ctx0: x2 -> P10
        }
        mesh.load_instr(P00.row, P00.col, addr, p00[addr]);
        mesh.load_instr(P01.row, P01.col, addr, p01[addr]);
        mesh.load_instr(P10.row, P10.col, addr, p10[addr]);
        advance_cycles(1);
    }
    mesh.clear_instr(P00.row, P00.col);
    mesh.clear_instr(P01.row, P01.col);
    mesh.clear_instr(P10.row, P10.col);
    mesh.clear_instr(P00.row, 0);
    mesh.clear_instr(P10.row, 0);
}

static void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh.load_instr(0, 1, addr, relay);  // Vectorial: x1 -> P00
        mesh.load_instr(0, 2, addr, relay);  // Escalar:   x3 -> P01
        advance_cycles(1);
    }
    mesh.clear_instr(0, 1);
    mesh.clear_instr(0, 2);
}

//============================================================================
// Un caso de prueba: 4 logits enteros y su etiqueta.
//============================================================================
struct SoftmaxCase {
    std::string label;
    std::array<int32_t, 4> x;
};

static bool run_case(Mesh& mesh, sc_signal<bool>& rst,
                      sc_signal<Link>* in_N, sc_signal<Link>* in_W, sc_signal<Link>* out_W,
                      int case_num, const SoftmaxCase& tc) {
    // Referencia independiente: numerador exacto 2^(x_i) via shift de
    // int64_t (no confia en que la malla haga bien la cuenta), luego
    // softmax_2 = numerador / suma. La division es exactamente el paso que
    // el enunciado deja fuera de la malla -- aca, en C++, es donde ocurre.
    std::array<int64_t, 4> numerator;
    int64_t sum_ref = 0;
    for (int i = 0; i < 4; i++) {
        numerator[i] = int64_t(1) << (tc.x[i] + EXP2_SHIFT_BIAS);
        sum_ref += numerator[i];
    }

    cout << "\n============================================================\n"
         << "  CASO " << case_num << " -- " << tc.label << "\n"
         << "============================================================\n"
         << "      x = [" << tc.x[0] << ", " << tc.x[1] << ", " << tc.x[2] << ", " << tc.x[3] << "]\n"
         << "      SUM esperada (2^x0+2^x1+2^x2+2^x3) = " << sum_ref << "\n";

    in_W[P00.row].write(Link({tc.x[0]}));
    in_N[1].write(Link({tc.x[1]}));
    in_W[P10.row].write(Link({tc.x[2]}));
    in_N[2].write(Link({tc.x[3]}));

    test_section((std::string("Caso ") + std::to_string(case_num) + ": ejecucion").c_str());
    advance_cycles(8);   // addr0..7: settle + EXP2 local + relevos + suma parcial
    advance_cycles(1);   // addr8: SUM completa en P00
    // Routing(1,0) ctx1, con 2 ciclos de margen antes de addr10 (mismo
    // motivo que en los otros 3 TB finales).
    mesh.load_instr(P00.row, 0, 1, routing_relay_out());
    advance_cycles(1);   // addr9: settle
    advance_cycles(1);   // addr10: SUM -> out_W[1]
    int64_t sum_got = (int64_t)(uint32_t)(int32_t)out_W[P00.row].read()[0];

    advance_cycles(1);   // addr11: e0 -> out_W[1] (spot-check)
    int64_t e0_got = (int64_t)(uint32_t)(int32_t)out_W[P00.row].read()[0];

    mesh.clear_instr(P00.row, 0);

    bool ok = true;
    auto check = [&](const char* label, int64_t expected, int64_t got) {
        bool pass = (expected == got);
        cout << (pass ? "PASS " : "FAIL ") << label << "  esperado=" << expected << "  obtenido=" << got << "\n";
        if (!pass) ok = false;
    };
    cout << "      SUM obtenida = " << sum_got << "   e0 obtenido = " << e0_got << "\n\n";
    check("SUM", sum_ref, sum_got);
    check("e0 (2^x0, spot-check)", numerator[0], e0_got);

    cout << "\n      softmax_2(x) = 2^x_i / SUM  (division hecha aca, fuera de la malla):\n";
    for (int i = 0; i < 4; i++) {
        double s = double(numerator[i]) / double(sum_ref);
        cout << "        x" << i << "=" << std::setw(3) << tc.x[i]
             << "   2^x" << i << "=" << std::setw(10) << numerator[i]
             << "   softmax_2=" << std::fixed << std::setprecision(6) << s << "\n";
    }
    cout << std::defaultfloat;

    cout << "\n  Resultado del caso " << case_num << ": " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

static void gen_random_case(std::array<int32_t, 4>& x, int lo, int hi) {
    for (int i = 0; i < 4; i++) x[i] = lo + std::rand() % (hi - lo + 1);
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
         << "#  Softmax base-2 de 4 elementos sobre CGRA_Final_Mesh       #\n"
         << "#  (0,0)=Memoria (sin uso) (0,1)=Vectorial  (0,2)=Escalar    #\n"
         << "#  (1,0)=Routing  (1,1)=MAC(P00, hub)  (1,2)=MAC(P01)        #\n"
         << "#  (2,0)=Routing  (2,1)=MAC(P10)       (2,2)=MAC (sin uso)   #\n"
         << "#  La malla hace 2^x + reduccion; la division es en C++.     #\n"
         << "############################################################\n";

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < CGRA_FINAL_COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < CGRA_FINAL_ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    advance_cycles(2);
    rst.write(false);
    enable.write(true);

    Instr p00[PROG_LEN], p01[PROG_LEN], p10[PROG_LEN];
    build_softmax_program(p00, p01, p10);

    test_section("Programacion inicial: relays de Vectorial/Escalar + EXP2 + arbol de suma");
    setup_relays(mesh);
    load_softmax_program(mesh, rst, p00, p01, p10);
    advance_cycles(PROG_LEN);  // margen: primera vuelta con el programa ya asentado

    std::srand(20260810);
    std::vector<SoftmaxCase> cases(3);
    cases[0].label = "logits iguales (softmax deberia dar 1/4 parejo)";
    cases[0].x = {0, 0, 0, 0};
    cases[1].label = "un logit dominante";
    cases[1].x = {0, 0, 6, 0};
    cases[2].label = "logits aleatorios con signo";
    gen_random_case(cases[2].x, -9, 9);

    bool all_ok = true;
    for (size_t c = 0; c < cases.size(); c++) {
        bool ok = run_case(mesh, rst, in_N, in_W, out_W, (int)c + 1, cases[c]);
        all_ok = all_ok && ok;
        if (c + 1 < cases.size()) {
            test_section("Entre casos: recargar programa");
            load_softmax_program(mesh, rst, p00, p01, p10);
            advance_cycles(PROG_LEN);
        }
    }

    cout << "\n############################################################\n";
    if (all_ok) {
        cout << "#  PASS: softmax base-2 correcto en los 3 casos             #\n";
    } else {
        cout << "#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n";
    }
    cout << "############################################################\n";

    return all_ok ? 0 : 1;
}
