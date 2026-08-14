// FFT4_NoC_HLS_Top_C__TB.cpp
// Testbench plano para FFT4_NoC_HLS_Top_C: 3 casos (misma semilla 20260810
// y mismos vectores que CGRA_Final_FFT4_C__TB.cpp) contra una DFT de 4 puntos
// de referencia (fuerza bruta, independiente del programa a/b/c/d que usa la
// malla).

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "FFT4_NoC_HLS_Top_C.h"

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

struct FftCase {
    std::string label;
    Cplx x[4];
};

static bool run_case(int case_num, const FftCase& tc) {
    Cplx Xref[4];
    dft4_reference(tc.x, Xref);

    Cplx got[4];
    FFT4_NoC_HLS_Top_C(
        tc.x[0].re, tc.x[0].im, tc.x[1].re, tc.x[1].im,
        tc.x[2].re, tc.x[2].im, tc.x[3].re, tc.x[3].im,
        &got[0].re, &got[0].im, &got[1].re, &got[1].im,
        &got[2].re, &got[2].im, &got[3].re, &got[3].im);

    bool ok = true;
    static const char* names[4] = {"X0", "X1", "X2", "X3"};
    printf("CASO %d -- %s\n  x0=%s x1=%s x2=%s x3=%s\n", case_num, tc.label.c_str(),
           cplx_str(tc.x[0]).c_str(), cplx_str(tc.x[1]).c_str(),
           cplx_str(tc.x[2]).c_str(), cplx_str(tc.x[3]).c_str());
    for (int k = 0; k < 4; k++) {
        bool pass = (Xref[k].re == got[k].re && Xref[k].im == got[k].im);
        printf("  %s %s esperado=%s obtenido=%s -> %s\n", pass ? "PASS" : "FAIL", names[k],
               cplx_str(Xref[k]).c_str(), cplx_str(got[k]).c_str(), pass ? "PASS" : "FAIL");
        if (!pass) ok = false;
    }
    printf("\n");
    return ok;
}

static void gen_random_cplx(Cplx x[4], int lo, int hi) {
    for (int n = 0; n < 4; n++) {
        x[n].re = lo + std::rand() % (hi - lo + 1);
        x[n].im = lo + std::rand() % (hi - lo + 1);
    }
}

int main() {
    printf("############################################################\n"
           "#  FFT de 4 puntos sobre FFT4_NoC_HLS_Top_C (malla NoC 3x3) #\n"
           "############################################################\n\n");

    std::srand(20260810);
    FftCase cases[3];
    cases[0].label = "impulso en x1 (deberia dar X_k = W4^k)";
    cases[0].x[0] = {0, 0}; cases[0].x[1] = {1, 0}; cases[0].x[2] = {0, 0}; cases[0].x[3] = {0, 0};
    cases[1].label = "escalon [1,1,0,0]";
    cases[1].x[0] = {1, 0}; cases[1].x[1] = {1, 0}; cases[1].x[2] = {0, 0}; cases[1].x[3] = {0, 0};
    cases[2].label = "complejo aleatorio";
    gen_random_cplx(cases[2].x, -9, 9);

    bool all_ok = true;
    for (int c = 0; c < 3; c++) all_ok = run_case(c + 1, cases[c]) && all_ok;

    printf("============================================================\n");
    printf(all_ok ? "RESULTADO: PASS (los 3 casos)\n" : "RESULTADO: FAIL\n");
    printf("============================================================\n");
    return all_ok ? 0 : 1;
}
