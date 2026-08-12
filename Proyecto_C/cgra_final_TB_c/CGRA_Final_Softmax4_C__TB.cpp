// CGRA_Final_Softmax4_C__TB.cpp
// Transliteracion a C/C++ puro de cgra_final_TB/CGRA_Final_Softmax4__TB.cpp:
// softmax de 4 elementos sobre la malla final 3x3 (cgra_final_c/CGRA_Final_Mesh_C.h).
//
// Esta ISA no tiene exponencial ni division (ver pe_isa_hls_c.h: ADD/SUB/AND/
// OR/XOR/shifts/comparaciones/MUL/MAC, nada mas), asi que un softmax "exacto"
// de punto flotante no es posible en la malla. Este testbench reproduce lo que
// hace un acelerador real: separa la parte PARALELIZABLE (exponencial por
// elemento + reduccion de la suma) de la parte ESCALAR (una unica division por
// todo el vector, que no se beneficia de paralelizarse). La malla hace la
// primera parte; run_case() hace la segunda en C++, fuera del array.
//
// exp2 exacto via shift (no una aproximacion con error): en vez de e^x, la
// malla computa 2^x escalado -- un softmax de BASE 2. softmax_2(x) =
// 2^x / sum(2^x) es exactamente softmax_e evaluado en logits pre-escalados por
// ln(2), asi que sigue siendo un softmax legitimo (mismo orden relativo, misma
// forma), solo con otra temperatura -- y es EXACTO, porque 2^x de un entero x
// es un shift, no una serie truncada. Para representar 2^x con enteros aun con
// x negativo, todo se escala por una constante K=9 fija:
//   EXP2(x) = 1 << (x+9)
// valido sin overflow ni resultado negativo para x en [-9, 9], computable en
// una unica OP_SLL con el shift-amount armado por un ADD previo.
//
// Arbol de reduccion (2 niveles, log2(4)=2): las 4 entradas usan un borde real
// distinto cada una, sin multiplexado en el tiempo.
//
//   x0 -> in_W[1] -> Routing(1,0) ctx0 -> P00(oeste)  -- e0 = EXP2(x0)
//   x1 -> in_N[1] -> Vectorial(0,1)    -> P00(norte)  -- e1 = EXP2(x1)
//   x2 -> in_W[2] -> Routing(2,0) ctx0 -> P10(oeste)  -- e2 = EXP2(x2)
//   x3 -> in_N[2] -> Escalar(0,2)      -> P01(norte)  -- e3 = EXP2(x3)
//
//   P00 ya tiene e0,e1 localmente; P10 --e2(norte)--> P00(sur);
//   P01 --e3(oeste)--> P00(este) -- P00 es vecino directo de ambos, asi que no
//   hace falta una celda P11 combinadora como en la reduccion de 8.
//
//   P00: SUM = e0+e1+e2+e3 -> Routing(1,0) ctx1 -> out_W[1]
//   P00: e0 (spot-check)   -> mismo puerto, un ciclo despues
//
// Memoria (0,0) y MAC(2,2) quedan sin participar.
//
// CALENDARIO respecto del original: 11 slots en vez de 12, y los EXP2 de las 4
// entradas arrancan todos en el mismo par de slots -- en el original x1/x3
// (via Vectorial/Escalar) llegaban un ciclo mas tarde que x0/x2 (via Routing)
// y habia que escalonar los ADD/SLL. En C los cuatro caminos son de 1 ciclo
// (ver la nota de temporizado en CGRA_Final_Mesh_C.h), asi que van parejos.

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
static const int32_t EXP2_SHIFT_BIAS = 9;  // EXP2(x) = 1 << (x + 9), valido para x en [-9, 9]

struct Coord { int row, col; };
static const Coord P00 = {1, 1};
static const Coord P01 = {1, 2};
static const Coord P10 = {2, 1};

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
static Instr mov_reg_instr(ap_uint<5> reg_a, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
static Instr add_imm_to_reg(ap_uint<3> src, int32_t imm, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = src; i.src_b = SRC_IMM; i.imm = imm;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
// reg[reg_dst] = 1 << reg[shamt_reg]  (EXP2 exacto via shift)
static Instr shl_one_by_reg(ap_uint<5> shamt_reg, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_SLL; i.src_a = SRC_IMM; i.imm = 1; i.src_b = SRC_REG; i.reg_b = shamt_reg;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr accum_reg_and_reg(ap_uint<5> reg_a, ap_uint<5> reg_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = SRC_REG; i.reg_b = reg_b;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr accum_reg_and_dir(ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr nop_instr() { return Instr(); }

// Registros de P00: 0=tmp0/e0, 1=tmp1/e1, 2=SUM (se reusan tmp->e en el mismo
// indice porque para cuando se calcula e_i ya no hace falta tmp_i).
static void build_softmax_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN]) {
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); }

    // addr1: los 4 bordes ya estan presentados (addr0) -- sesgo +9 en paralelo.
    p00[1] = add_imm_to_reg(SRC_WEST,  EXP2_SHIFT_BIAS, 0);   // tmp0 = x0+9
    p10[1] = add_imm_to_reg(SRC_WEST,  EXP2_SHIFT_BIAS, 0);   // tmp2 = x2+9
    p01[1] = add_imm_to_reg(SRC_NORTH, EXP2_SHIFT_BIAS, 0);   // tmp3 = x3+9

    // addr2: EXP2 de x2/x3; P00 sesga su segunda entrada (norte, via Vectorial).
    p00[2] = add_imm_to_reg(SRC_NORTH, EXP2_SHIFT_BIAS, 1);   // tmp1 = x1+9
    p10[2] = shl_one_by_reg(0, 0);                            // e2 = 1<<tmp2
    p01[2] = shl_one_by_reg(0, 0);                            // e3 = 1<<tmp3

    // addr3/addr4: EXP2 de x0 y x1 en P00 (una instruccion por ciclo).
    p00[3] = shl_one_by_reg(0, 0);                            // e0 = 1<<tmp0
    p00[4] = shl_one_by_reg(1, 1);                            // e1 = 1<<tmp1

    // addr4: P10/P01 relevan e2/e3 hacia P00 (vecinos directos).
    p10[4] = mov_reg_instr(0, DST_NORTH);                     // e2 -> P00 (sur de P00)
    p01[4] = mov_reg_instr(0, DST_WEST);                      // e3 -> P00 (este de P00)

    // addr5..7: P00 arma la suma de las 4 exponenciales.
    p00[5] = accum_reg_and_reg(0, 1, 2);                      // reg2 = e0+e1
    p00[6] = accum_reg_and_dir(2, SRC_SOUTH, 2);              // reg2 += e2
    p00[7] = accum_reg_and_dir(2, SRC_EAST, 2);               // reg2 += e3 = SUM

    // addr8/9: saca SUM y despues e0 (spot-check) por el mismo puerto, en dos
    // tiempos distintos -- mismo patron que C[0][0] en el TB de GEMM.
    p00[8] = mov_reg_instr(2, DST_WEST);                      // SUM -> Routing ctx1 -> out_W[1]
    p00[9] = mov_reg_instr(0, DST_WEST);                      // e0  -> mismo puerto
}

static Instr routing_relay_in()  {  // ctx0: borde real W -> enlace interno E
    return make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {  // ctx1: enlace interno E -> borde real W
    return make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Programacion (0 ciclos)
//============================================================================
static void load_softmax_program(Mesh& mesh, const Instr p00[PROG_LEN],
                                  const Instr p01[PROG_LEN], const Instr p10[PROG_LEN]) {
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh_program(mesh, P00.row, P00.col, addr, p00[addr]);
        mesh_program(mesh, P01.row, P01.col, addr, p01[addr]);
        mesh_program(mesh, P10.row, P10.col, addr, p10[addr]);
    }
}

static void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh_program(mesh, 0, 1, addr, relay);  // Vectorial: x1 -> P00
        mesh_program(mesh, 0, 2, addr, relay);  // Escalar:   x3 -> P01
    }
}

static void arm_case(Mesh& mesh) {
    step_n(mesh, 1, /*rst=*/true);
    mesh_program(mesh, P00.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(1,0): x0 -> P00
    mesh_program(mesh, P10.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(2,0): x2 -> P10
}

//============================================================================
// Un caso de prueba: 4 logits enteros y su etiqueta.
//============================================================================
struct SoftmaxCase {
    std::string label;
    int32_t x[4];
};

static bool run_case(Mesh& mesh, int case_num, const SoftmaxCase& tc) {
    // Referencia independiente: numerador exacto 2^(x_i) via shift de int64_t
    // (no confia en que la malla haga bien la cuenta), luego
    // softmax_2 = numerador / suma. La division es exactamente el paso que se
    // deja fuera de la malla -- aca, en C++, es donde ocurre.
    int64_t numerator[4];
    int64_t sum_ref = 0;
    for (int i = 0; i < 4; i++) {
        numerator[i] = int64_t(1) << (tc.x[i] + EXP2_SHIFT_BIAS);
        sum_ref += numerator[i];
    }

    printf("\n============================================================\n"
           "  CASO %d -- %s\n"
           "============================================================\n"
           "      x = [%d, %d, %d, %d]\n"
           "      SUM esperada (2^x0+2^x1+2^x2+2^x3) = %lld\n",
           case_num, tc.label.c_str(), tc.x[0], tc.x[1], tc.x[2], tc.x[3], (long long)sum_ref);

    g_in_W[P00.row] = lane0(tc.x[0]);
    g_in_N[1]       = lane0(tc.x[1]);
    g_in_W[P10.row] = lane0(tc.x[2]);
    g_in_N[2]       = lane0(tc.x[3]);

    test_section((std::string("Caso ") + std::to_string(case_num) + ": ejecucion").c_str());
    step_n(mesh, 8);   // addr0..7: relays + EXP2 local + relevos + SUM completa en P00
    step_n(mesh, 1);   // addr8: SUM -> puerto oeste de P00

    // Conmutar Routing(1,0) a ctx1 para sacar los resultados al borde real.
    mesh_program(mesh, P00.row, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1);   // addr9: e0 -> puerto oeste de P00 ; Routing saca SUM al borde
    int64_t sum_got = (int64_t)mesh.cell<1, 0>().out_W[0].to_int();

    step_n(mesh, 1);   // addr10: Routing saca e0 al borde
    int64_t e0_got = (int64_t)mesh.cell<1, 0>().out_W[0].to_int();

    bool ok = true;
    printf("      SUM obtenida = %lld   e0 obtenido = %lld\n\n", (long long)sum_got, (long long)e0_got);
    test_check(ok, "SUM", "x del caso " + std::to_string(case_num), sum_ref, sum_got);
    test_check(ok, "e0 (2^x0, spot-check)", "x0=" + std::to_string(tc.x[0]), numerator[0], e0_got);

    printf("\n      softmax_2(x) = 2^x_i / SUM  (division hecha aca, fuera de la malla):\n");
    for (int i = 0; i < 4; i++) {
        double s = double(numerator[i]) / double(sum_ref);
        printf("        x%d=%3d   2^x%d=%10lld   softmax_2=%.6f\n", i, tc.x[i], i, (long long)numerator[i], s);
    }

    printf("\n  Resultado del caso %d: %s\n", case_num, ok ? "PASS" : "FAIL");
    return ok;
}

static void gen_random_case(int32_t x[4], int lo, int hi) {
    for (int i = 0; i < 4; i++) x[i] = lo + std::rand() % (hi - lo + 1);
}

int main() {
    Mesh mesh;

    printf("\n############################################################\n"
           "#  Softmax base-2 de 4 elementos sobre CGRA_Final_Mesh_C     #\n"
           "#  (0,0)=Memoria (sin uso) (0,1)=Vectorial  (0,2)=Escalar    #\n"
           "#  (1,0)=Routing  (1,1)=MAC(P00, hub)  (1,2)=MAC(P01)        #\n"
           "#  (2,0)=Routing  (2,1)=MAC(P10)       (2,2)=MAC (sin uso)   #\n"
           "#  La malla hace 2^x + reduccion; la division es en C++.     #\n"
           "############################################################\n");

    test_section("Reset");
    step_n(mesh, 1, /*rst=*/true);

    Instr p00[PROG_LEN], p01[PROG_LEN], p10[PROG_LEN];
    build_softmax_program(p00, p01, p10);

    test_section("Programacion inicial: relays de Vectorial/Escalar + EXP2 + arbol de suma");
    setup_relays(mesh);
    load_softmax_program(mesh, p00, p01, p10);

    std::srand(20260810);
    SoftmaxCase cases[3];
    cases[0].label = "logits iguales (softmax deberia dar 1/4 parejo)";
    cases[0].x[0] = 0; cases[0].x[1] = 0; cases[0].x[2] = 0; cases[0].x[3] = 0;
    cases[1].label = "un logit dominante";
    cases[1].x[0] = 0; cases[1].x[1] = 0; cases[1].x[2] = 6; cases[1].x[3] = 0;
    cases[2].label = "logits aleatorios con signo";
    gen_random_case(cases[2].x, -9, 9);

    bool all_ok = true;
    for (int c = 0; c < 3; c++) {
        test_section((std::string("Preparar caso ") + std::to_string(c + 1) +
                      ": realinear pc + recargar Routing").c_str());
        arm_case(mesh);
        all_ok = run_case(mesh, c + 1, cases[c]) && all_ok;
    }

    printf("\n############################################################\n");
    if (all_ok) {
        printf("#  PASS: softmax base-2 correcto en los 3 casos             #\n");
    } else {
        printf("#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n");
    }
    printf("############################################################\n");

    return all_ok ? 0 : 1;
}
