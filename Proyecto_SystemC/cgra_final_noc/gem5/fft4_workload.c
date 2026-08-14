// fft4_workload.c
// Reimplementacion standalone del contrato aritmetico de la FFT de 4 puntos
// de la CGRA_Final_NoC_Mesh (radix-2, decimacion en frecuencia): mismas
// ecuaciones a=x0+x2, b=x0-x2, c=x1+x3, d=(x1-x3)*(-j), X0=a+c, X1=b+d,
// X2=a-c, X3=b-d que la malla computa con ADD/SUB puro (ver el comentario de
// cabecera de
// ../../../../Proyecto_C/cgra_final_noc_c/FFT4_NoC_HLS_Top_C.h para por que
// N=4 evita necesitar multiplicacion real). Mismos 3 casos (semilla 20260810)
// que FFT4_NoC_HLS_Top_C__TB.cpp.
//
// NOTA sobre que mide esto vs. la CGRA real: ver gemm_workload.c -- este
// binario mide un CPU host haciendo la misma mariposa de 4 puntos
// secuencialmente; ../vitis/fft4/ sintetiza la malla real (4 celdas MAC
// computando a/b/c/d en paralelo espacial) via Vitis HLS.
//
// Compilar (nativo x86_64, estatico):
//   gcc -O0 -static -o fft4_workload fft4_workload.c

#include <stdio.h>
#include <stdint.h>

typedef struct { int32_t re, im; } Cplx;

// v * (-j): mismo twiddle que via el empaquetado de lanes calcula la malla
// (ver comentario de cabecera de FFT4_NoC_HLS_Top_C.h).
static Cplx twiddle_neg_j(Cplx v) {
    Cplx r; r.re = v.im; r.im = -v.re; return r;
}

static void fft4(volatile Cplx *x, Cplx X[4]) {
    Cplx x0 = {x[0].re, x[0].im}, x1 = {x[1].re, x[1].im};
    Cplx x2 = {x[2].re, x[2].im}, x3 = {x[3].re, x[3].im};

    Cplx a = {x0.re + x2.re, x0.im + x2.im};
    Cplx b = {x0.re - x2.re, x0.im - x2.im};
    Cplx c = {x1.re + x3.re, x1.im + x3.im};
    Cplx d = twiddle_neg_j((Cplx){x1.re - x3.re, x1.im - x3.im});

    X[0] = (Cplx){a.re + c.re, a.im + c.im};
    X[1] = (Cplx){b.re + d.re, b.im + d.im};
    X[2] = (Cplx){a.re - c.re, a.im - c.im};
    X[3] = (Cplx){b.re - d.re, b.im - d.im};
}

typedef struct {
    const char *label;
    Cplx x[4];
    Cplx X_expected[4];
} Round;

static const Round kRounds[] = {
    {"Ronda 1 (impulso en x1)",
     {{0, 0}, {1, 0}, {0, 0}, {0, 0}},
     {{1, 0}, {0, -1}, {-1, 0}, {0, 1}}},
    {"Ronda 2 (escalon [1,1,0,0])",
     {{1, 0}, {1, 0}, {0, 0}, {0, 0}},
     {{2, 0}, {1, -1}, {0, 0}, {1, 1}}},
    {"Ronda 3 (complejo aleatorio)",
     {{-6, -2}, {-6, -7}, {0, 2}, {-1, 9}},
     {{-13, 2}, {-22, 1}, {1, -2}, {10, -9}}},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        volatile Cplx x[4];
        for (int i = 0; i < 4; i++) x[i] = kRounds[r].x[i];

        Cplx X[4];
        fft4(x, X);

        int ok = 1;
        for (int k = 0; k < 4; k++)
            if (X[k].re != kRounds[r].X_expected[k].re || X[k].im != kRounds[r].X_expected[k].im) ok = 0;
        all_ok = all_ok && ok;

        printf("%s: X=[%d%+dj,%d%+dj,%d%+dj,%d%+dj] -> %s\n", kRounds[r].label,
               X[0].re, X[0].im, X[1].re, X[1].im, X[2].re, X[2].im, X[3].re, X[3].im,
               ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
