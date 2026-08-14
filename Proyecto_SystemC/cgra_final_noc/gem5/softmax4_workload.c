// softmax4_workload.c
// Reimplementacion standalone del contrato aritmetico del softmax base-2 de
// 4 elementos de la CGRA_Final_NoC_Mesh: EXP2(x)=1<<(x+9) (exacto via shift,
// valido para x en [-9,9] -- ver el comentario de cabecera de
// ../../../../Proyecto_C/cgra_final_noc_c/Softmax4_NoC_HLS_Top_C.h) y
// SUM=sum(EXP2(x_i)); la division softmax_i=EXP2(x_i)/SUM es la parte escalar
// que un acelerador real deja fuera del array, para el host -- aca se hace en
// C, fuera de sum_reduce_exp2(), igual que en el testbench de referencia.
// Mismos 3 casos (semilla 20260810) que Softmax4_NoC_HLS_Top_C__TB.cpp.
//
// NOTA sobre que mide esto vs. la CGRA real: ver gemm_workload.c -- este
// binario mide un CPU host calculando 4 EXP2 y su suma secuencialmente;
// ../vitis/softmax4/ sintetiza la malla real (3 celdas MAC computando EXP2 en
// paralelo espacial + arbol de reduccion de 2 niveles) via Vitis HLS.
//
// Compilar (nativo x86_64, estatico):
//   gcc -O0 -static -o softmax4_workload softmax4_workload.c

#include <stdio.h>
#include <stdint.h>

#define EXP2_SHIFT_BIAS 9  // EXP2(x) = 1 << (x + 9), valido para x en [-9, 9]

static int32_t exp2_shift(int32_t x) { return (int32_t)1 << (x + EXP2_SHIFT_BIAS); }

static int32_t sum_reduce_exp2(volatile int32_t *x, int32_t e[4]) {
    int32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        e[i] = exp2_shift(x[i]);
        sum += e[i];
    }
    return sum;
}

typedef struct {
    const char *label;
    int32_t x[4];
    int32_t sum_expected;
} Round;

static const Round kRounds[] = {
    {"Ronda 1 (logits iguales)",    {0, 0, 0, 0},  2048},
    {"Ronda 2 (un logit dominante)",{0, 0, 6, 0},  34304},
    {"Ronda 3 (aleatorios)",        {-6, -2, -6, -7}, 148},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        volatile int32_t x[4];
        for (int i = 0; i < 4; i++) x[i] = kRounds[r].x[i];

        int32_t e[4];
        int32_t sum = sum_reduce_exp2(x, e);
        int ok = (sum == kRounds[r].sum_expected);
        all_ok = all_ok && ok;

        printf("%s: SUM=%d expected=%d -> %s\n", kRounds[r].label, sum, kRounds[r].sum_expected,
               ok ? "PASS" : "FAIL");
        printf("  softmax_2(x) = 2^x_i / SUM (division fuera del acelerador):\n");
        for (int i = 0; i < 4; i++)
            printf("    x%d=%3d  2^x%d=%8d  softmax_2=%.6f\n", i, x[i], i, e[i], (double)e[i] / (double)sum);
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
