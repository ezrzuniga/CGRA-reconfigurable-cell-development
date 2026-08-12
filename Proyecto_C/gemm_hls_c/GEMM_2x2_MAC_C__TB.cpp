// GEMM_2x2_MAC_C__TB.cpp
// Transliteracion a C/C++ puro de gemm_hls/GEMM_2x2_MAC__TB.cpp: CGRA 2x2 de
// mapeo espacial para GEMM (una celda PE_MAC por elemento de salida,
// output-stationary, sin routing ni memoria), manejada DIRECTAMENTE con
// mesh_step() desde el testbench.
//
// Complementa a GEMM_2x2_HLS_Top_C__TB.cpp: aquel ejercita la misma malla pero
// a traves del top sintetizable (cgra_run<...>, con su FSM de fases interna);
// este la maneja ciclo a ciclo desde afuera, que es la forma de verificar que
// el programa espacial en si es correcto, independientemente de la FSM que lo
// orqueste. Malla y programa en GEMM_2x2_Mesh_C.h -- una sola fuente de verdad,
// compartida por los dos testbenches y por el top.
//
// El calendario de 4 ciclos por fase de k del original vale TAL CUAL aca: en
// esta malla 2x2 dedicada las 4 celdas son bordes reales, asi que tanto A
// (in_W[fila]) como B (in_N[columna]) entran sin ningun relay intermedio -- no
// existe la asimetria Routing/Vectorial que si obliga a reescalonar el
// calendario en la malla final 3x3 (ver cgra_final_TB_c/CGRA_Final_GEMM_C__TB.cpp).
//
//        slot0            slot1            slot2            slot3
// P00  MOV W->E(a)     MOV N->S(b)      MAC(W,N)->ACC    MOV ACC->W
// P01  MOV N->S(b)     MAC(W,N)->ACC    NOP              MOV ACC->E
// P10  MOV W->E(a)     NOP              MAC(W,N)->ACC    MOV ACC->W
// P11  NOP             MAC(W,N)->ACC    NOP              MOV ACC->E

#include <cstdio>
#include <cstdint>
#include <string>
#include "GEMM_2x2_Mesh_C.h"
#include "../pe_hls_c/test_util_c.h"

typedef GemmMesh_C  Mesh;
typedef GemmLink_C  Link;
typedef GemmInstr_C Instr;

static const int ROWS = GEMM_ROWS;
static const int COLS = GEMM_COLS;

static Link g_in_N[COLS], g_in_S[COLS], g_in_W[ROWS], g_in_E[ROWS];

static void step_n(Mesh& mesh, int n, bool rst = false) {
    for (int i = 0; i < n; i++) mesh_step(mesh, rst, /*enable=*/true, g_in_N, g_in_S, g_in_W, g_in_E);
    test_count_cycles(n);
}

static Link lane0(int32_t v) { Link l; l[0] = v; return l; }

// Carga los 4 slots en las 4 celdas. A diferencia del original (una direccion
// por ciclo, porque load_instr pasaba por un sc_signal muestreado en el flanco)
// esto no consume ni un ciclo: mesh_program() escribe instr_mem directo.
static void load_program(Mesh& mesh, const Instr prog[ROWS][COLS][GEMM_INSTR_MEM_SIZE]) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            for (int addr = 0; addr < GEMM_INSTR_MEM_SIZE; addr++)
                mesh_program(mesh, r, c, addr, prog[r][c][addr]);
}

struct GemmCase {
    const char* label;
    int32_t A[2][2];
    int32_t B[2][2];
};

static void run_case(Mesh& mesh, bool& ok, const GemmCase& tc) {
    int32_t C[2][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += tc.A[i][k] * tc.B[k][j];
            C[i][j] = sum;
        }

    test_section(std::string("Caso: ") + tc.label);
    printf("A=[[%d,%d],[%d,%d]] B=[[%d,%d],[%d,%d]] C esperado=[[%d,%d],[%d,%d]]\n",
           tc.A[0][0], tc.A[0][1], tc.A[1][0], tc.A[1][1],
           tc.B[0][0], tc.B[0][1], tc.B[1][0], tc.B[1][1],
           C[0][0], C[0][1], C[1][0], C[1][1]);

    for (int k = 0; k < 2; k++) {
        g_in_W[0] = lane0(tc.A[0][k]);
        g_in_W[1] = lane0(tc.A[1][k]);
        g_in_N[0] = lane0(tc.B[k][0]);
        g_in_N[1] = lane0(tc.B[k][1]);
        step_n(mesh, GEMM_INSTR_MEM_SIZE);   // una vuelta completa del programa
    }

    std::string in = std::string("A,B del caso '") + tc.label + "'";
    test_check(ok, "C[0][0] (out_W[0])", in, C[0][0], mesh.cell<0, 0>().out_W[0].to_int());
    test_check(ok, "C[0][1] (out_E[0])", in, C[0][1], mesh.cell<0, 1>().out_E[0].to_int());
    test_check(ok, "C[1][0] (out_W[1])", in, C[1][0], mesh.cell<1, 0>().out_W[0].to_int());
    test_check(ok, "C[1][1] (out_E[1])", in, C[1][1], mesh.cell<1, 1>().out_E[0].to_int());
}

int main() {
    Mesh mesh;
    bool ok = true;

    test_section("Reset");
    step_n(mesh, 1, /*rst=*/true);

    Instr prog[ROWS][COLS][GEMM_INSTR_MEM_SIZE];
    gemm_program_c(prog);

    test_section("Carga del programa espacial GEMM (4 slots x 4 PEs, 0 ciclos)");
    load_program(mesh, prog);

    GemmCase case1;
    case1.label = "enteros positivos";
    case1.A[0][0] = 1; case1.A[0][1] = 2; case1.A[1][0] = 3; case1.A[1][1] = 4;
    case1.B[0][0] = 5; case1.B[0][1] = 6; case1.B[1][0] = 7; case1.B[1][1] = 8;
    run_case(mesh, ok, case1);

    // rst no limpia el acumulador de PE_MAC (mismo precedente que el original)
    // -- entre casos hay que limpiarlo a mano. mesh_clear_acc() es un canal
    // lateral directo: ya no hace falta pisar instr_mem con clear_acc en las 4
    // direcciones ni el pulso de rst extra para volver a alinear el pc que ese
    // truco desalineaba. Un unico rst alcanza para dejar los 4 pc en 0.
    test_section("Entre casos: limpiar acumuladores (canal lateral) + realinear pc");
    mesh_clear_acc(mesh);
    step_n(mesh, 1, /*rst=*/true);

    GemmCase case2;
    case2.label = "con valores negativos";
    case2.A[0][0] = -3; case2.A[0][1] = 5;  case2.A[1][0] = 2;  case2.A[1][1] = -4;
    case2.B[0][0] = 6;  case2.B[0][1] = -1; case2.B[1][0] = -2; case2.B[1][1] = 3;
    run_case(mesh, ok, case2);

    if (ok) {
        printf("\nPASS: CGRA 2x2 de mapeo espacial (PE_MAC x4) resuelve GEMM 2x2 "
               "en ambos casos de prueba.\n");
    }
    return ok ? 0 : 1;
}
