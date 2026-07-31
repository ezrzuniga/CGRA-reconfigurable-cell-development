// GEMM_2x2_HLS_Top__TB.cpp
// Testbench del wrapper sintetizable GEMM_2x2_HLS_Top: solo usa los puertos
// reales del top (clk/rst/enable/start/done/a00..c11) -- nada de load_instr()
// ni advance_cycles() como atajo sobre la malla interna, eso ya no aplica, es
// hardware real controlado por su propia FSM. Mismos dos casos de prueba que
// GEMM_2x2_MAC__TB.cpp (mismos valores de A/B/C esperados).
#include <systemc.h>
#include <cstdint>
#include <string>
#include "GEMM_2x2_HLS_Top.h"
#include "../pe/test_util.h"

struct GemmCase {
    const char* label;
    int32_t A[2][2];
    int32_t B[2][2];
};

static bool wait_done(sc_signal<bool>& done, int max_cycles) {
    for (int i = 0; i < max_cycles; i++) {
        advance_cycles(1);
        if (done.read()) return true;
    }
    return false;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable, start, done;
    sc_signal<sc_int<32>> a00, a01, a10, a11;
    sc_signal<sc_int<32>> b00, b01, b10, b11;
    sc_signal<sc_int<32>> c00, c01, c10, c11;

    GEMM_2x2_HLS_Top top("top");
    top.clk(clk); top.rst(rst); top.enable(enable); top.start(start); top.done(done);
    top.a00(a00); top.a01(a01); top.a10(a10); top.a11(a11);
    top.b00(b00); top.b01(b01); top.b10(b10); top.b11(b11);
    top.c00(c00); top.c01(c01); top.c10(c10); top.c11(c11);

    test_section("Reset");
    rst.write(true);
    enable.write(true);
    start.write(false);
    a00.write(0); a01.write(0); a10.write(0); a11.write(0);
    b00.write(0); b01.write(0); b10.write(0); b11.write(0);
    advance_cycles(2);
    rst.write(false);
    // Boot (carga del programa espacial en las 4 PEs) corre solo, sin start.
    advance_cycles(10);

    bool ok = true;

    GemmCase cases[2] = {
        {"enteros positivos",
         {{1, 2}, {3, 4}},
         {{5, 6}, {7, 8}}},
        {"con valores negativos",
         {{-3, 5}, {2, -4}},
         {{6, -1}, {-2, 3}}},
    };

    for (const GemmCase& tc : cases) {
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

        a00.write(tc.A[0][0]); a01.write(tc.A[0][1]);
        a10.write(tc.A[1][0]); a11.write(tc.A[1][1]);
        b00.write(tc.B[0][0]); b01.write(tc.B[0][1]);
        b10.write(tc.B[1][0]); b11.write(tc.B[1][1]);

        start.write(true);
        advance_cycles(1);
        start.write(false);

        bool finished = wait_done(done, 40);
        test_check_bool(ok, "done se activo", tc.label, finished);

        if (finished) {
            test_check(ok, "C[0][0]", tc.label, sc_int<32>(C[0][0]), c00.read());
            test_check(ok, "C[0][1]", tc.label, sc_int<32>(C[0][1]), c01.read());
            test_check(ok, "C[1][0]", tc.label, sc_int<32>(C[1][0]), c10.read());
            test_check(ok, "C[1][1]", tc.label, sc_int<32>(C[1][1]), c11.read());
        } else {
            ok = false;
        }
    }

    if (ok) {
        cout << "\nPASS: GEMM_2x2_HLS_Top (FSM + malla real) resuelve GEMM 2x2 "
             << "en ambos casos de prueba, controlado solo por start/done." << endl;
    }
    return ok ? 0 : 1;
}
