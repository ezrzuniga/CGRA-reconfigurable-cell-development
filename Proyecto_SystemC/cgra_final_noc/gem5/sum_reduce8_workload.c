// sum_reduce8_workload.c
// Reimplementacion standalone del contrato aritmetico de la reduccion de 8
// enteros de la CGRA_Final_NoC_Mesh: TOTAL = v0+v1+...+v7. Mismos 3 casos
// (misma semilla 20260810) que
// ../../../../Proyecto_C/cgra_final_noc_c/SumReduce8_NoC_HLS_Top_C__TB.cpp.
//
// NOTA sobre que mide esto vs. la CGRA real: ver el comentario de cabecera de
// gemm_workload.c (mismo razonamiento) -- gem5 mide un CPU host ejecutando
// esta reduccion secuencial de 8 elementos; ../vitis/sum_reduce8/ sintetiza
// el arbol de reduccion REAL de la CGRA (4 celdas MAC en paralelo, arbol
// binario de 3 niveles) via Vitis HLS. Comparar los ciclos de ambos es
// precisamente el punto: cuanto gana la reduccion en arbol paralelo del
// acelerador frente a la suma secuencial de un CPU de proposito general.
//
// Compilar (nativo x86_64, estatico):
//   gcc -O0 -static -o sum_reduce8_workload sum_reduce8_workload.c
//
// -O0 a proposito (ver gemm_workload.c): sin el, el compilador colapsa la
// suma de 8 constantes a un resultado precomputado.

#include <stdio.h>
#include <stdint.h>

#define SUM_LEN 8

static int32_t sum_reduce8(volatile int32_t *v) {
    int32_t total = 0;
    for (int i = 0; i < SUM_LEN; i++) total += v[i];
    return total;
}

typedef struct {
    const char *label;
    int32_t v[SUM_LEN];
    int32_t expected_total;
} Round;

static const Round kRounds[] = {
    {"Ronda 1 (unos y ceros)",     {1, 1, 1, 1, 1, 1, 1, 1},       8},
    {"Ronda 2 (potencias de dos)", {1, 2, 4, 8, 16, 32, 64, 128},  255},
    {"Ronda 3 (aleatorios)",       {-3, 1, 17, 20, 2, 0, 13, 7},   57},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        volatile int32_t v[SUM_LEN];
        for (int i = 0; i < SUM_LEN; i++) v[i] = kRounds[r].v[i];

        int32_t total = sum_reduce8(v);
        int ok = (total == kRounds[r].expected_total);
        all_ok = all_ok && ok;

        printf("%s: total=%d expected=%d -> %s\n",
               kRounds[r].label, total, kRounds[r].expected_total, ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
