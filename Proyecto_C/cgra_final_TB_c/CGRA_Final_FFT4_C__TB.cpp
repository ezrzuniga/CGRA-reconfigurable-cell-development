// CGRA_Final_FFT4_C__TB.cpp
// Transliteracion a C/C++ puro de cgra_final_TB/CGRA_Final_FFT4__TB.cpp: FFT
// de 4 puntos (radix-2, decimacion en frecuencia) sobre la malla final 3x3
// (cgra_final_c/CGRA_Final_Mesh_C.h), reusando el mismo bloque de celdas MAC
// que el TB de GEMM pero como motor generico ADD/SUB en vez de MAC/ACC: las 4
// celdas MAC tienen ALU completa (ver PE_MAC_HLS_C.h), asi que aca se
// reprograman para computar mariposas (butterflies) de FFT.
//
// Por que N=4 y no un tamano mayor: todos los factores twiddle de una FFT de 4
// puntos son +-1 o +-j (W4^0=1, W4^1=-j, W4^2=-1, W4^3=j) -- multiplicar por
// cualquiera de ellos es una permutacion de (real,imag) con signo, nunca una
// multiplicacion real. Eso evita necesitar OP_MUL para los twiddles y encaja
// con la ALU entera de esta ISA sin punto fijo.
//
// Complejos: cada dato x=(re,im) viaja en un unico Link de 4 lanes, pero la
// ALU aplica el MISMO opcode a las 4 lanes por igual (SIMD sin permutacion
// entre lanes) -- no hay forma de "intercambiar" real e imaginario dentro de
// una instruccion. La solucion: el testbench arma el VECTOR DE ENTRADA con un
// patron de lanes distinto segun si esa entrada necesita el twiddle -j:
//   entradas SIN twiddle (x0, x2):    [re, im, re, im]
//   entradas CON twiddle -j (x1, x3): [re, im, im, -re]
// Con ese empaquetado, una unica resta x1-x3 tiene el resultado NORMAL en las
// lanes 0-1 (sin usar) y, gratis, el resultado YA MULTIPLICADO por -j en las
// lanes 2-3 -- toda la aritmetica queda en ADD/SUB puro.
//
// Por que x3 NO entra por Escalar: PE_Scalar_State (PE_Scalar_HLS_C.h) puentea
// Link <-> escalar tomando SOLO la lane 0 en la entrada y haciendo broadcast
// en la salida, lo que destruye la parte imaginaria de cualquier complejo que
// pase por ahi. Escalar (0,2) queda entonces FUERA del camino de datos: los 4
// complejos entran solo por los bordes que preservan las 4 lanes intactas
// (in_W[1], in_W[2] y in_N[1] via Vectorial).
//
// Con solo 2 bordes "completos" utiles para 4 entradas, x2 y x3 comparten
// in_W[2] por multiplexado en el tiempo: el testbench escribe x2 primero, deja
// que Routing(2,0)+P10 la releven hacia P00, y RECIEN ENTONCES sobreescribe
// in_W[2] con x3 -- que P10 releva hacia el este (P11) en vez de hacia el
// norte. Nada se pisa porque el primer relay de P10 ya "se llevo" x2 antes de
// que el puerto cambie.
//
// Flujo espacial completo (DIF, entrada en orden natural x0..x3):
//   x0 -> in_W[1] -> Routing(1,0) ctx0 -> P00(oeste)
//   x1 -> in_N[1] -> Vectorial(0,1)    -> P00(norte) -> relay este  -> P01(oeste)
//   x2 -> in_W[2] -> Routing(2,0) ctx0 -> P10(oeste) -> relay norte -> P00(sur)
//   x3 -> in_W[2] (reescrito)          -> P10(oeste) -> relay este  -> P11(oeste)
//                                                    -> relay norte -> P01(sur)
//   P00: a = x0+x2 (reg0), b = x0-x2 (reg1)
//   P01: c = x1+x3 (reg0), d = twiddle(x1-x3) (reg1)
//   P01 --c--> P00(este)   P00 --b--> P01(oeste)
//   P00: X0 = a+c, X2 = a-c (lanes 0-1) -> Routing(1,0) ctx1 -> out_W[1]
//   P01: X1 = b+d, X3 = b-d (lanes 2-3) -> out_E[1] (borde real directo)
//
// CALENDARIO respecto del original: 10 slots en vez de 14. Ademas de los
// relays alineados (ver la nota de temporizado en CGRA_Final_Mesh_C.h), en C
// desaparecen los "slots de settle" que el original intercalaba para cubrir el
// retardo extra de un borde recien escrito desde sc_main.

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

static Link link4(int32_t a, int32_t b, int32_t c, int32_t d) {
    Link v; v[0] = a; v[1] = b; v[2] = c; v[3] = d; return v;
}

//============================================================================
// Constructores de instruccion
//============================================================================
static Instr mov_instr(ap_uint<3> src, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
static Instr mov_reg_instr(ap_uint<5> reg_a, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
static Instr op_to_reg(ap_uint<4> op, ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = op; i.src_a = src_a; i.src_b = src_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr op_reg_and_dir(ap_uint<4> op, ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<3> dst) {
    Instr i; i.opcode = op; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b; i.dst = dst; return i;
}
static Instr op_dir_and_reg(ap_uint<4> op, ap_uint<3> src_a, ap_uint<5> reg_b, ap_uint<3> dst) {
    Instr i; i.opcode = op; i.src_a = src_a; i.src_b = SRC_REG; i.reg_b = reg_b; i.dst = dst; return i;
}
static Instr nop_instr() { return Instr(); }

// Programa espacial de la FFT de 4 puntos: una cadena secuencial de
// dependencias, sin patron repetitivo (no hay pasos k=0/k=1 como en GEMM).
static void build_fft_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN], Instr p11[PROG_LEN]) {
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); p11[a] = nop_instr(); }

    p10[1] = mov_instr(SRC_WEST, DST_NORTH);                 // x2 -> P00 (sur)
    p00[1] = mov_instr(SRC_NORTH, DST_EAST);                 // x1 -> P01 (oeste)

    // Aca el testbench reescribe in_W[2] con x3: P10 ya se llevo x2.
    p00[2] = op_to_reg(OP_ADD, SRC_WEST, SRC_SOUTH, 0);      // reg0 = a = x0+x2
    p00[3] = op_to_reg(OP_SUB, SRC_WEST, SRC_SOUTH, 1);      // reg1 = b = x0-x2
    p10[3] = mov_instr(SRC_WEST, DST_EAST);                  // x3 (ya presentado) -> P11 (oeste)
    p11[4] = mov_instr(SRC_WEST, DST_NORTH);                 // x3 -> P01 (sur)

    p01[5] = op_to_reg(OP_ADD, SRC_WEST, SRC_SOUTH, 0);      // reg0 = c = x1+x3
    p01[6] = op_to_reg(OP_SUB, SRC_WEST, SRC_SOUTH, 1);      // reg1 = d = twiddle(x1-x3)

    // b se releva RECIEN aca (no antes): P00.out_E es el mismo puerto que
    // llevo x1 hacia P01 (addr1), y P01 todavia lo necesita via SRC_WEST en
    // addr5/6 -- pisarlo antes corrompe esa lectura.
    p00[7] = mov_reg_instr(1, DST_EAST);                     // b -> P01
    p01[7] = mov_reg_instr(0, DST_WEST);                     // c -> P00

    p00[8] = op_reg_and_dir(OP_ADD, 0, SRC_EAST, DST_WEST);  // X0 = a+c (lanes 0-1) -> Routing ctx1
    p01[8] = op_dir_and_reg(OP_ADD, SRC_WEST, 1, DST_EAST);  // X1 = b+d (lanes 2-3) -> out_E[1]

    p00[9] = op_reg_and_dir(OP_SUB, 0, SRC_EAST, DST_WEST);  // X2 = a-c (lanes 0-1) -> Routing ctx1
    p01[9] = op_dir_and_reg(OP_SUB, SRC_WEST, 1, DST_EAST);  // X3 = b-d (lanes 2-3) -> out_E[1]
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
static void load_fft_program(Mesh& mesh, const Instr p00[PROG_LEN], const Instr p01[PROG_LEN],
                              const Instr p10[PROG_LEN], const Instr p11[PROG_LEN]) {
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh_program(mesh, P00.row, P00.col, addr, p00[addr]);
        mesh_program(mesh, P01.row, P01.col, addr, p01[addr]);
        mesh_program(mesh, P10.row, P10.col, addr, p10[addr]);
        mesh_program(mesh, P11.row, P11.col, addr, p11[addr]);
    }
}

// Solo Vectorial participa (releva x1 hacia P00) -- Escalar queda fuera del
// camino de datos de esta FFT (ver comentario de cabecera).
static void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++)
        mesh_program(mesh, 0, 1, addr, relay);  // Vectorial: x1 -> P00
}

static void arm_case(Mesh& mesh) {
    step_n(mesh, 1, /*rst=*/true);
    mesh_program(mesh, P00.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(1,0): x0 -> P00
    mesh_program(mesh, P10.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(2,0): x2/x3 -> P10
}

//============================================================================
// Complejo entero minimo + DFT de referencia (fuerza bruta, independiente de
// las ecuaciones a/b/c/d que usa la malla) para validar cruzado.
//============================================================================
struct Cplx { int32_t re, im; };

static Cplx cmul_root(Cplx v, int power) {  // v * W4^power, W4^power en {1,-j,-1,j}
    switch (((power % 4) + 4) % 4) {
        case 0: return {v.re, v.im};
        case 1: return {v.im, -v.re};
        case 2: return {-v.re, -v.im};
        default: return {-v.im, v.re};
    }
}

static void dft4_reference(const Cplx x[4], Cplx X[4]) {
    for (int k = 0; k < 4; k++) {
        Cplx sum{0, 0};
        for (int n = 0; n < 4; n++) {
            Cplx term = cmul_root(x[n], k * n);
            sum.re += term.re;
            sum.im += term.im;
        }
        X[k] = sum;
    }
}

static std::string cplx_str(const Cplx& c) {
    return std::to_string(c.re) + (c.im >= 0 ? "+" : "") + std::to_string(c.im) + "j";
}

//============================================================================
// Un caso de prueba
//============================================================================
struct FftCase {
    std::string label;
    Cplx x[4];
};

static bool run_case(Mesh& mesh, int case_num, const FftCase& tc) {
    Cplx Xref[4];
    dft4_reference(tc.x, Xref);

    printf("\n============================================================\n"
           "  CASO %d -- %s\n"
           "============================================================\n"
           "      x0=%s x1=%s x2=%s x3=%s\n"
           "      X esperado: X0=%s X1=%s X2=%s X3=%s\n",
           case_num, tc.label.c_str(),
           cplx_str(tc.x[0]).c_str(), cplx_str(tc.x[1]).c_str(), cplx_str(tc.x[2]).c_str(), cplx_str(tc.x[3]).c_str(),
           cplx_str(Xref[0]).c_str(), cplx_str(Xref[1]).c_str(), cplx_str(Xref[2]).c_str(), cplx_str(Xref[3]).c_str());

    // Empaquetado de lanes (ver comentario de cabecera). x3 todavia no se
    // escribe: comparte in_W[2] con x2 por multiplexado en el tiempo.
    g_in_W[P00.row] = link4(tc.x[0].re, tc.x[0].im, tc.x[0].re,  tc.x[0].im);
    g_in_N[1]       = link4(tc.x[1].re, tc.x[1].im, tc.x[1].im, -tc.x[1].re);
    g_in_W[P10.row] = link4(tc.x[2].re, tc.x[2].im, tc.x[2].re,  tc.x[2].im);

    test_section((std::string("Caso ") + std::to_string(case_num) + ": ejecucion").c_str());
    step_n(mesh, 2);   // addr0: relays presentan x0/x1/x2 | addr1: x2 -> P00(sur), x1 -> P01(oeste)

    // Ahora in_W[2] = x3: P10 ya relevo x2 en addr1, nada se pisa.
    g_in_W[P10.row] = link4(tc.x[3].re, tc.x[3].im, tc.x[3].im, -tc.x[3].re);
    step_n(mesh, 6);   // addr2..7: a, b, x3->P11->P01, c, d, intercambio b<->c
    step_n(mesh, 1);   // addr8: X0 = a+c (P00), X1 = b+d (P01)

    Cplx X1{ mesh.cell<1, 2>().out_E[2].to_int(), mesh.cell<1, 2>().out_E[3].to_int() };

    // Conmutar Routing(1,0) a ctx1 para sacar X0/X2 al borde real oeste.
    mesh_program(mesh, P00.row, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1);   // addr9: X2/X3 en las celdas ; Routing saca X0 al borde
    Cplx X0{ mesh.cell<1, 0>().out_W[0].to_int(), mesh.cell<1, 0>().out_W[1].to_int() };
    Cplx X3{ mesh.cell<1, 2>().out_E[2].to_int(), mesh.cell<1, 2>().out_E[3].to_int() };

    step_n(mesh, 1);   // addr10: Routing saca X2 al borde
    Cplx X2{ mesh.cell<1, 0>().out_W[0].to_int(), mesh.cell<1, 0>().out_W[1].to_int() };

    printf("      X obtenido: X0=%s X1=%s X2=%s X3=%s\n\n",
           cplx_str(X0).c_str(), cplx_str(X1).c_str(), cplx_str(X2).c_str(), cplx_str(X3).c_str());

    bool ok = true;
    Cplx got[4] = {X0, X1, X2, X3};
    static const char* names[4] = {"X0", "X1", "X2", "X3"};
    for (int k = 0; k < 4; k++) {
        bool pass = (Xref[k].re == got[k].re && Xref[k].im == got[k].im);
        printf("%s %s  esperado=%s  obtenido=%s\n", pass ? "PASS" : "FAIL", names[k],
               cplx_str(Xref[k]).c_str(), cplx_str(got[k]).c_str());
        if (!pass) ok = false;
    }

    printf("  Resultado del caso %d: %s\n", case_num, ok ? "PASS" : "FAIL");
    return ok;
}

static void gen_random_cplx(Cplx x[4], int lo, int hi) {
    for (int n = 0; n < 4; n++) {
        x[n].re = lo + std::rand() % (hi - lo + 1);
        x[n].im = lo + std::rand() % (hi - lo + 1);
    }
}

int main() {
    Mesh mesh;

    printf("\n############################################################\n"
           "#  FFT de 4 puntos sobre CGRA_Final_Mesh_C (layout 3x3)      #\n"
           "#  (0,0)=Memoria (sin uso) (0,1)=Vectorial (0,2)=Escalar(-)  #\n"
           "#  (1,0)=Routing  (1,1)=MAC(P00)   (1,2)=MAC(P01)            #\n"
           "#  (2,0)=Routing  (2,1)=MAC(P10)   (2,2)=MAC(P11, relevo)    #\n"
           "############################################################\n");

    test_section("Reset");
    step_n(mesh, 1, /*rst=*/true);

    Instr p00[PROG_LEN], p01[PROG_LEN], p10[PROG_LEN], p11[PROG_LEN];
    build_fft_program(p00, p01, p10, p11);

    test_section("Programacion inicial: relay de Vectorial + programa de mariposas");
    setup_relays(mesh);
    load_fft_program(mesh, p00, p01, p10, p11);

    std::srand(20260810);
    FftCase cases[3];
    cases[0].label = "impulso en x1 (deberia dar X_k = W4^k)";
    cases[0].x[0] = {0, 0}; cases[0].x[1] = {1, 0}; cases[0].x[2] = {0, 0}; cases[0].x[3] = {0, 0};
    cases[1].label = "escalon [1,1,0,0]";
    cases[1].x[0] = {1, 0}; cases[1].x[1] = {1, 0}; cases[1].x[2] = {0, 0}; cases[1].x[3] = {0, 0};
    cases[2].label = "complejo aleatorio";
    gen_random_cplx(cases[2].x, -9, 9);

    bool all_ok = true;
    for (int c = 0; c < 3; c++) {
        test_section((std::string("Preparar caso ") + std::to_string(c + 1) +
                      ": realinear pc + recargar Routing").c_str());
        arm_case(mesh);
        all_ok = run_case(mesh, c + 1, cases[c]) && all_ok;
    }

    printf("\n############################################################\n");
    if (all_ok) {
        printf("#  PASS: FFT4 correcta en los 3 casos sobre la CGRA final   #\n");
    } else {
        printf("#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n");
    }
    printf("############################################################\n");

    return all_ok ? 0 : 1;
}
