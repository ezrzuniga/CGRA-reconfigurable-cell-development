// GEMM_2x2_MAC__TB.cpp
// CGRA 2x2 de mapeo espacial para GEMM: una PE_MAC_Cell_HLS por elemento de
// salida (output-stationary), sin routing/memoria -- ver plan de diseno.
// Malla y programa en GEMM_2x2_Mesh.h (compartido con GEMM_2x2_HLS_Top.h, el
// wrapper sintetizable que controla la misma malla desde una FSM en vez de
// desde este testbench).
#include <systemc.h>
#include <cstdint>
#include <string>
#include <sstream>
#include "GEMM_2x2_Mesh.h"
#include "../pe/test_util.h"

static const int ROWS = GEMM_ROWS;
static const int COLS = GEMM_COLS;
typedef GemmMesh Mesh;
typedef GemmLink Link;
typedef GemmInstr Instr;

// Carga un programa de 4 slots en las 4 PEs, una direccion por ciclo (mismo
// patron de load_instr+advance_cycles(1) que los demas testbenches de mesh_hls).
static void load_program(Mesh& mesh, const Instr prog[ROWS][COLS][4]) {
    for (int addr = 0; addr < 4; addr++) {
        gemm_load_program(mesh, prog, addr);
        advance_cycles(1);
    }
    gemm_clear_all_instr(mesh);
}

// rst no limpia el acumulador de PE_MAC_HLS (mismo precedente que PE_MAC.h) --
// entre dos casos de prueba hace falta limpiarlo a mano. Escribe clear_acc en
// las 4 direcciones (una por ciclo, misma semantica read-old-then-load-new que
// load_program) y despues un ciclo mas para que de verdad se ejecute desde
// cualquiera de las 4 direcciones -- ya quedaron todas con la misma instruccion.
static void clear_all_acc(Mesh& mesh) {
    for (int addr = 0; addr < 4; addr++) {
        gemm_load_clear_acc(mesh, addr);
        advance_cycles(1);
    }
    advance_cycles(1);
    gemm_clear_all_instr(mesh);
}

struct GemmCase {
    const char* label;
    int32_t A[2][2];
    int32_t B[2][2];
};

static void run_case(Mesh& mesh, sc_signal<Link>* in_W, sc_signal<Link>* in_N,
                      bool& ok, const GemmCase& tc) {
    int32_t C[2][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += tc.A[i][k] * tc.B[k][j];
            C[i][j] = sum;
        }

    test_section(std::string("Caso: ") + tc.label);
    cout << "A=[[" << tc.A[0][0] << "," << tc.A[0][1] << "],[" << tc.A[1][0] << "," << tc.A[1][1] << "]] "
         << "B=[[" << tc.B[0][0] << "," << tc.B[0][1] << "],[" << tc.B[1][0] << "," << tc.B[1][1] << "]] "
         << "C esperado=[[" << C[0][0] << "," << C[0][1] << "],[" << C[1][0] << "," << C[1][1] << "]]" << endl;

    for (int k = 0; k < 2; k++) {
        in_W[0].write(Link({tc.A[0][k]}));
        in_W[1].write(Link({tc.A[1][k]}));
        in_N[0].write(Link({tc.B[k][0]}));
        in_N[1].write(Link({tc.B[k][1]}));
        advance_cycles(4);
    }

    std::ostringstream in;
    in << "A,B del caso '" << tc.label << "'";
    test_check(ok, "C[0][0] (out_W[0])", in.str(), sc_int<32>(C[0][0]), mesh.out_W[0].read()[0]);
    test_check(ok, "C[0][1] (out_E[0])", in.str(), sc_int<32>(C[0][1]), mesh.out_E[0].read()[0]);
    test_check(ok, "C[1][0] (out_W[1])", in.str(), sc_int<32>(C[1][0]), mesh.out_W[1].read()[0]);
    test_check(ok, "C[1][1] (out_E[1])", in.str(), sc_int<32>(C[1][1]), mesh.out_E[1].read()[0]);
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

    Mesh mesh("mesh");
    mesh.clk(clk);
    mesh.rst(rst);
    mesh.enable(enable);
    for (int c = 0; c < COLS; c++) {
        mesh.in_N[c](in_N[c]);   mesh.out_N[c](out_N[c]);
        mesh.in_S[c](in_S[c]);   mesh.out_S[c](out_S[c]);
    }
    for (int r = 0; r < ROWS; r++) {
        mesh.in_W[r](in_W[r]);   mesh.out_W[r](out_W[r]);
        mesh.in_E[r](in_E[r]);   mesh.out_E[r](out_E[r]);
    }

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    advance_cycles(2);
    rst.write(false);
    enable.write(true);

    Instr prog[ROWS][COLS][4];
    gemm_program(prog);

    test_section("Carga del programa espacial GEMM (4 slots x 4 PEs)");
    load_program(mesh, prog);

    bool ok = true;

    GemmCase case1;
    case1.label = "enteros positivos";
    case1.A[0][0] = 1; case1.A[0][1] = 2; case1.A[1][0] = 3; case1.A[1][1] = 4;
    case1.B[0][0] = 5; case1.B[0][1] = 6; case1.B[1][0] = 7; case1.B[1][1] = 8;
    run_case(mesh, in_W, in_N, ok, case1);

    test_section("Reset entre casos + limpieza de acumuladores");
    rst.write(true);
    enable.write(false);
    advance_cycles(2);
    rst.write(false);
    enable.write(true);
    clear_all_acc(mesh);
    // clear_all_acc no consume un multiplo de 4 ciclos (el ciclo extra que
    // ejecuta clear_acc de verdad corre el pc uno de mas) -- otro pulso de
    // rst realinea pc a 0 sin tocar acc (ya en 0) ni instr_mem, para que
    // load_program vuelva a arrancar exactamente en el slot0 del programa.
    rst.write(true);
    advance_cycles(1);
    rst.write(false);
    load_program(mesh, prog);

    GemmCase case2;
    case2.label = "con valores negativos";
    case2.A[0][0] = -3; case2.A[0][1] = 5;  case2.A[1][0] = 2;  case2.A[1][1] = -4;
    case2.B[0][0] = 6;  case2.B[0][1] = -1; case2.B[1][0] = -2; case2.B[1][1] = 3;
    run_case(mesh, in_W, in_N, ok, case2);

    if (ok) {
        cout << "\nPASS: CGRA 2x2 de mapeo espacial (PE_MAC_Cell_HLS x4) resuelve GEMM 2x2 "
             << "en ambos casos de prueba." << endl;
    }
    return ok ? 0 : 1;
}
