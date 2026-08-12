// CGRA_Final_FFT4__TB.cpp
// FFT de 4 puntos (radix-2, decimacion en frecuencia) sobre la malla final 3x3
// (cgra_final/CGRA_Final_Mesh.h), reusando el mismo bloque de celdas MAC que
// CGRA_Final_GEMM__TB.cpp pero como motor generico ADD/SUB en vez de MAC/ACC:
// las 4 celdas MAC tienen ALU completa (ver pe_hls/mac/PE_MAC_HLS.h), no solo
// multiplicar-acumular, asi que aca se reprograman para computar mariposas
// (butterflies) de FFT.
//
// Por que N=4 y no un tamano mayor: todos los factores twiddle de una FFT de
// 4 puntos son +-1 o +-j (W4^0=1, W4^1=-j, W4^2=-1, W4^3=j) -- multiplicar por
// cualquiera de ellos es una permutacion de (real,imag) con signo, nunca una
// multiplicacion real. Eso evita necesitar OP_MUL para los twiddles (que si
// haria falta desde N=8 en adelante, con W8^1=(1-j)/sqrt(2)) y encaja con la
// ALU entera de esta ISA sin punto fijo.
//
// Complex numbers: cada dato complejo x=(re,im) viaja en un unico wire de la
// malla (PE_VectorData<32,4>, 4 lanes), pero el ALU de esta ISA aplica el
// MISMO opcode a las 4 lanes por igual (SIMD sin permutacion entre lanes) --
// no hay forma de "intercambiar" real e imaginario dentro de una instruccion.
// La solucion: el testbench arma el VECTOR DE ENTRADA con un patron de lanes
// distinto segun si esa entrada necesita el twiddle -j mas adelante:
//   entradas SIN twiddle (x0, x2):      [re, im, re, im]  (duplicado simple)
//   entradas CON twiddle -j (x1, x3):   [re, im, im, -re] (real/imag ya
//     pre-permutados en las lanes 2-3, listos para la resta que sigue)
// Con ese empaquetado, una unica resta x1-x3 tiene el resultado NORMAL en
// las lanes 0-1 (sin usar) y, gratis, el resultado YA MULTIPLICADO por -j en
// las lanes 2-3 (justo lo que hace falta para el twiddle W4^1) -- toda la
// aritmetica de la FFT queda en ADD/SUB puro, el truco esta 100% en como el
// testbench arma las lanes de la entrada.
//
// Por que x3 NO entra por Escalar (a diferencia de como B entraba por
// Escalar en CGRA_Final_GEMM__TB): PE_Scalar_Cell_HLS puentea Link (4 lanes)
// <-> el sc_int<32> escalar de PE_scalar_HLS tomando SOLO la lane 0 en la
// entrada y haciendo broadcast de un unico valor en la salida (ver
// PE_Scalar_Cell_HLS::bridge_in_N/bridge_out_N) -- funciona para GEMM porque
// B era un escalar real de una sola lane util, pero aca destruye la parte
// imaginaria de cualquier complejo que pase por ahi. Por eso Escalar (0,2)
// queda FUERA del camino de datos de esta FFT: los 4 complejos entran solo
// por los 3 bordes reales que preservan las 4 lanes intactas (in_W[1],
// in_W[2], in_N[1] vía Vectorial, que SI pasa el Link completo).
//
// Con solo 3 bordes "completos" para 4 entradas, x2 y x3 comparten in_W[2]
// por multiplexado en el tiempo (mismo patron que compartir un puerto entre
// dos pasos, como el k=0/k=1 de CGRA_Final_GEMM__TB): el testbench escribe
// x2 primero, deja que Routing(2,0)+P10 la releven hacia P00, y RECIEN
// ENTONCES sobreescribe in_W[2] con x3 -- que P10 releva hacia el otro lado
// (este, hacia P11) en vez de hacia el norte. Nada se pisa porque el primer
// relay de P10 (hacia P00) ya "se llevo" el valor de x2 antes de que el
// puerto cambie.
//
// Flujo espacial completo (DIF, entrada en orden natural x0..x3, salida en
// orden bit-invertido X0,X2,X1,X3 -- no hace falta reordenar nada, cada
// salida sale por su propio puerto/instante y el testbench la etiqueta como
// corresponde):
//
//   x0 -> in_W[1] -> Routing(1,0) ctx0 -> P00(oeste)
//   x1 -> in_N[1] -> Vectorial(0,1)    -> P00(norte) -> relay este  -> P01(oeste)
//   x2 -> in_W[2] -> Routing(2,0) ctx0 -> P10(oeste) -> relay norte -> P00(sur)
//   x3 -> in_W[2] (reescrito luego)    -> P10(oeste) -> relay este  -> P11(oeste)
//                                                     -> relay norte -> P01(sur)
//
//   P00: a = x0+x2, b = x0-x2               (guardados en reg0, reg1)
//   P01: c = x1+x3, d = twiddle(x1-x3)      (guardados en reg0, reg1)
//   P01 --c(reg0)--> P00 (oeste)   P00 --b(reg1)--> P01 (este)
//   P00: X0 = a+c (lanes 0-1), X2 = a-c (lanes 0-1)   -> Routing(1,0) ctx1 -> out_W[1]
//   P01: X1 = b+d (lanes 2-3), X3 = b-d (lanes 2-3)   -> out_E[1] (borde real directo)
//
// Memoria (0,0) y Escalar (0,2) no participan de este calculo -- Escalar por
// la incompatibilidad de arriba, Memoria porque ninguna FFT espacial de este
// tamano necesita SRAM. Igual que GEMM dejaba una celda ociosa, aca dos
// quedan fuera del camino de datos.

#include <systemc.h>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>
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
static Instr mov_reg_instr(sc_uint<5> reg_a, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
static Instr op_to_reg(sc_uint<4> op, sc_uint<3> src_a, sc_uint<3> src_b, sc_uint<5> reg_dst) {
    Instr i; i.opcode = op; i.src_a = src_a; i.src_b = src_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
static Instr op_reg_and_dir(sc_uint<4> op, sc_uint<5> reg_a, sc_uint<3> src_b, sc_uint<3> dst) {
    Instr i; i.opcode = op; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b; i.dst = dst; return i;
}
static Instr op_dir_and_reg(sc_uint<4> op, sc_uint<3> src_a, sc_uint<5> reg_b, sc_uint<3> dst) {
    Instr i; i.opcode = op; i.src_a = src_a; i.src_b = SRC_REG; i.reg_b = reg_b; i.dst = dst; return i;
}
static Instr nop_instr() { return Instr(); }

// Programa espacial de la FFT de 4 puntos, 16 direcciones (sin patron
// repetitivo -- a diferencia de CGRA_Final_GEMM__TB, aca no hay pasos k=0/k=1:
// las 4 entradas se escriben una unica vez (x3 en dos tiempos, ver cabecera)
// y el programa es una cadena secuencial de dependencias, cada address con
// margen de sobra respecto a cuando su entrada esta realmente lista (ver
// CGRA_Final_GEMM__TB.cpp para el porque del margen de 1 ciclo extra en cada
// relay de un borde recien escrito).
static void build_fft_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN], Instr p11[PROG_LEN]) {
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); p11[a] = nop_instr(); }

    p10[1] = mov_instr(SRC_WEST, DST_NORTH);                          // x2 -> P00 (sur)
    p00[2] = mov_instr(SRC_NORTH, DST_EAST);                          // x1 -> P01 (oeste)
    p10[3] = mov_instr(SRC_WEST, DST_EAST);                           // x3 (ya reescrito en in_W[2]) -> P11 (oeste)

    p00[4] = op_to_reg(OP_ADD, SRC_WEST, SRC_SOUTH, 0);               // reg0 = a = x0+x2
    p00[5] = op_to_reg(OP_SUB, SRC_WEST, SRC_SOUTH, 1);               // reg1 = b = x0-x2
    p11[5] = mov_instr(SRC_WEST, DST_NORTH);                          // x3 -> P01 (sur)

    p01[8] = op_to_reg(OP_ADD, SRC_WEST, SRC_SOUTH, 0);               // reg0 = c = x1+x3
    p01[9] = op_to_reg(OP_SUB, SRC_WEST, SRC_SOUTH, 1);               // reg1 = d = twiddle(x1-x3)

    // b se releva RECIEN aca (no antes): P00.out_E es el mismo cable que
    // lleva x1 hacia P01 (addr2), y P01 todavia lo necesita via SRC_WEST en
    // addr8/9 para c/diff -- pisarlo antes corrompe esa lectura.
    p00[10] = mov_reg_instr(1, DST_EAST);                             // envia b -> P01
    p01[10] = mov_reg_instr(0, DST_WEST);                             // envia c -> P00

    p00[12] = op_reg_and_dir(OP_ADD, 0, SRC_EAST, DST_WEST);          // X0 = a+c (lanes 0-1) -> Routing ctx1
    p01[12] = op_dir_and_reg(OP_ADD, SRC_WEST, 1, DST_EAST);          // X1 = b+d (lanes 2-3) -> out_E[1]

    p00[13] = op_reg_and_dir(OP_SUB, 0, SRC_EAST, DST_WEST);          // X2 = a-c (lanes 0-1) -> Routing ctx1
    p01[13] = op_dir_and_reg(OP_SUB, SRC_WEST, 1, DST_EAST);          // X3 = b-d (lanes 2-3) -> out_E[1]
}

static Instr routing_relay_in()  {  // ctx0: borde real W -> enlace interno E
    return make_routing_config_instr_hls<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {  // ctx1: enlace interno E -> borde real W
    return make_routing_config_instr_hls<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Carga de programas -- mismo patron de dos vueltas que CGRA_Final_GEMM__TB
// (una vuelta de 16 ciclos escribe instr_mem, la vuelta "margen" que sigue es
// la primera donde el programa ya asentado se ejecuta de verdad).
//============================================================================
static void load_fft_program(Mesh& mesh, sc_signal<bool>& rst,
                              const Instr p00[PROG_LEN], const Instr p01[PROG_LEN],
                              const Instr p10[PROG_LEN], const Instr p11[PROG_LEN]) {
    rst.write(true);
    advance_cycles(1);
    rst.write(false);

    for (int addr = 0; addr < PROG_LEN; addr++) {
        if (addr == 0) {
            mesh.load_instr(P00.row, 0, 0, routing_relay_in());   // Routing(1,0) ctx0: x0 -> P00
            mesh.load_instr(P10.row, 0, 0, routing_relay_in());   // Routing(2,0) ctx0: x2/x3 -> P10
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

// Solo Vectorial participa (releva x1 hacia P00) -- Escalar queda fuera del
// camino de datos de esta FFT (ver comentario de cabecera).
static void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh.load_instr(0, 1, addr, relay);  // Vectorial: x1 -> P00
        advance_cycles(1);
    }
    mesh.clear_instr(0, 1);
}

//============================================================================
// Complejo entero minimo + DFT de referencia (fuerza bruta, independiente de
// las ecuaciones a/b/c/d que usa la malla) para validar cruzado.
//============================================================================
struct Cplx { int32_t re, im; };

static Cplx cmul_root(Cplx v, int power) {  // v * W4^power, W4^power in {1,-j,-1,j}
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

static std::ostream& operator<<(std::ostream& os, const Cplx& c) {
    os << c.re << (c.im >= 0 ? "+" : "") << c.im << "j";
    return os;
}

//============================================================================
// Un caso de prueba
//============================================================================
struct FftCase {
    std::string label;
    Cplx x[4];
};

static bool run_case(Mesh& mesh, sc_signal<bool>& rst,
                      sc_signal<Link>* in_N, sc_signal<Link>* in_W,
                      sc_signal<Link>* out_W, sc_signal<Link>* out_E,
                      int case_num, const FftCase& tc) {
    Cplx Xref[4];
    dft4_reference(tc.x, Xref);

    cout << "\n============================================================\n"
         << "  CASO " << case_num << " -- " << tc.label << "\n"
         << "============================================================\n"
         << "      x0=" << tc.x[0] << " x1=" << tc.x[1] << " x2=" << tc.x[2] << " x3=" << tc.x[3] << "\n"
         << "      X esperado: X0=" << Xref[0] << " X1=" << Xref[1]
         << " X2=" << Xref[2] << " X3=" << Xref[3] << "\n";

    // Empaquetado de lanes: x0,x2 duplicados [re,im,re,im]; x1,x3 con el
    // patron [re,im,im,-re] que deja el twiddle -j listo en las lanes 2-3
    // de x1-x3 sin necesitar ninguna instruccion extra (ver comentario de
    // cabecera). x3 todavia no se escribe -- comparte in_W[2] con x2 por
    // multiplexado en el tiempo (ver mas abajo).
    in_W[P00.row].write(Link({tc.x[0].re, tc.x[0].im, tc.x[0].re, tc.x[0].im}));
    in_N[1].write(Link({tc.x[1].re, tc.x[1].im, tc.x[1].im, -tc.x[1].re}));
    in_W[P10.row].write(Link({tc.x[2].re, tc.x[2].im, tc.x[2].re, tc.x[2].im}));

    test_section((std::string("Caso ") + std::to_string(case_num) + ": ejecucion").c_str());
    advance_cycles(2);                                    // addr0..1: settle + x2 relevada hacia P00
    in_W[P10.row].write(Link({tc.x[3].re, tc.x[3].im, tc.x[3].im, -tc.x[3].re}));  // ahora in_W[2] = x3
    advance_cycles(4);                                    // addr2..5: x1->P01, x3->P11, a/b, x3->P01(via P11)
    advance_cycles(1);                                    // addr6: b -> P01
    advance_cycles(2);                                    // addr7..8: settle + c = x1+x3
    advance_cycles(1);                                    // addr9: d = twiddle(x1-x3)
    advance_cycles(1);                                    // addr10: c -> P00
    // Routing(1,0) ctx1 (igual que en CGRA_Final_GEMM__TB): un config recien
    // cargado tampoco se ve reflejado en el ciclo inmediato siguiente --
    // cargarlo aca, 2 ciclos antes de addr12, en vez de justo antes.
    mesh.load_instr(P00.row, 0, 1, routing_relay_out());
    advance_cycles(1);                                    // addr11: settle
    advance_cycles(1);                                    // addr12: X0 = a+c, X1 = b+d
    Cplx X0{ (int32_t)out_W[P00.row].read()[0], (int32_t)out_W[P00.row].read()[1] };
    Cplx X1{ (int32_t)out_E[P00.row].read()[2], (int32_t)out_E[P00.row].read()[3] };

    advance_cycles(1);                                    // addr13: X2 = a-c, X3 = b-d
    Cplx X2{ (int32_t)out_W[P00.row].read()[0], (int32_t)out_W[P00.row].read()[1] };
    Cplx X3{ (int32_t)out_E[P00.row].read()[2], (int32_t)out_E[P00.row].read()[3] };

    mesh.clear_instr(P00.row, 0);

    cout << "      X obtenido: X0=" << X0 << " X1=" << X1 << " X2=" << X2 << " X3=" << X3 << "\n\n";

    bool ok = true;
    auto check = [&](const char* label, Cplx expected, Cplx got) {
        bool pass = (expected.re == got.re && expected.im == got.im);
        cout << (pass ? "PASS " : "FAIL ") << label
             << "  esperado=" << expected << "  obtenido=" << got << "\n";
        if (!pass) ok = false;
    };
    check("X0", Xref[0], X0);
    check("X1", Xref[1], X1);
    check("X2", Xref[2], X2);
    check("X3", Xref[3], X3);

    cout << "  Resultado del caso " << case_num << ": " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

static void gen_random_cplx(Cplx x[4], int lo, int hi) {
    for (int n = 0; n < 4; n++) {
        x[n].re = lo + std::rand() % (hi - lo + 1);
        x[n].im = lo + std::rand() % (hi - lo + 1);
    }
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
         << "#  FFT de 4 puntos sobre CGRA_Final_Mesh (layout 3x3 final)  #\n"
         << "#  (0,0)=Memoria (sin uso) (0,1)=Vectorial (0,2)=Escalar(sin uso)\n"
         << "#  (1,0)=Routing  (1,1)=MAC(P00)   (1,2)=MAC(P01)            #\n"
         << "#  (2,0)=Routing  (2,1)=MAC(P10)   (2,2)=MAC(P11, relevo)    #\n"
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
    build_fft_program(p00, p01, p10, p11);

    test_section("Programacion inicial: relay de Vectorial + programa de mariposas");
    setup_relays(mesh);
    load_fft_program(mesh, rst, p00, p01, p10, p11);
    advance_cycles(PROG_LEN);  // margen: primera vuelta con el programa ya asentado

    std::srand(20260810);
    std::vector<FftCase> cases(3);
    cases[0].label = "impulso en x1 (deberia dar X_k = W4^k)";
    cases[0].x[0] = {0, 0}; cases[0].x[1] = {1, 0}; cases[0].x[2] = {0, 0}; cases[0].x[3] = {0, 0};
    cases[1].label = "escalon [1,1,0,0]";
    cases[1].x[0] = {1, 0}; cases[1].x[1] = {1, 0}; cases[1].x[2] = {0, 0}; cases[1].x[3] = {0, 0};
    cases[2].label = "complejo aleatorio";
    gen_random_cplx(cases[2].x, -9, 9);

    bool all_ok = true;
    for (size_t c = 0; c < cases.size(); c++) {
        bool ok = run_case(mesh, rst, in_N, in_W, out_W, out_E, (int)c + 1, cases[c]);
        all_ok = all_ok && ok;
        if (c + 1 < cases.size()) {
            test_section("Entre casos: recargar programa");
            load_fft_program(mesh, rst, p00, p01, p10, p11);
            advance_cycles(PROG_LEN);
        }
    }

    cout << "\n############################################################\n";
    if (all_ok) {
        cout << "#  PASS: FFT4 correcta en los 3 casos sobre la CGRA final   #\n";
    } else {
        cout << "#  FAIL: al menos un caso no coincidio (ver detalle arriba) #\n";
    }
    cout << "############################################################\n";

    return all_ok ? 0 : 1;
}
