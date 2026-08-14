// gemm_workload.c
// Reimplementacion standalone (sin SystemC, sin pragmas HLS, sin ap_int) del
// contrato aritmetico de la CGRA_Final_NoC_Mesh ejecutando GEMM 2x2:
// C = A*B. Mismos 3 casos (misma semilla 20260810) que
// ../../../../Proyecto_C/cgra_final_noc_c/GEMM_NoC_HLS_Top_C__TB.cpp, para
// que las 3 implementaciones (CGRA NoC real via Vitis HLS, este binario) sean
// comparables entre si.
//
// Por que este workload NO es "la CGRA corriendo en gem5": gem5 modela una
// CPU de proposito general ejecutando un binario -- no puede simular la
// malla NoC 3x3 heterogenea real (eso es exactamente lo que hace
// ../vitis/gemm/ via Vitis HLS: sintetiza esa malla de verdad). Este binario
// reproduce solo el CONTRATO aritmetico (mismas entradas, misma salida) para
// poder comparar "la misma tarea en un CPU host" vs "la misma tarea en el
// acelerador CGRA" -- exactamente el mismo precedente que
// ../../../../Proyecto_GEM5/sum_reduction_workload.c ya establecio para la
// reduccion de 7 elementos de mesh_wrapper (un diseno distinto a este).
//
// Compilar (nativo x86_64, estatico para que gem5 no dependa del linker
// dinamico del sistema en SE mode):
//   gcc -O0 -static -o gemm_workload gemm_workload.c
//
// -O0 a proposito: a plena optimizacion el compilador colapsa este kernel
// (multiplicaciones de constantes conocidas en tiempo de compilacion) a un
// resultado precomputado, y el conteo de ciclos que gem5 mide deja de
// reflejar "ejecutar el kernel real".

#include <stdio.h>
#include <stdint.h>

// volatile: refuerza a -O0 que no debe pre-calcular nada en tiempo de
// compilacion (defensivo, redundante con -O0 pero documenta la intencion).
static void gemm_2x2(volatile int32_t A[2][2], volatile int32_t B[2][2], int32_t C[2][2]) {
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += A[i][k] * B[k][j];
            C[i][j] = sum;
        }
    }
}

typedef struct {
    const char *label;
    int32_t A[2][2];
    int32_t B[2][2];
    int32_t C_expected[2][2];
} Round;

static const Round kRounds[] = {
    {"Ronda 1", {{-6, -2}, {-6, -7}}, {{0, 2}, {-1, 9}},   {{2, -30}, {7, -75}}},
    {"Ronda 2", {{-3, -9}, {3, -7}},  {{1, -7}, {-1, -4}}, {{6, 57}, {10, 7}}},
    {"Ronda 3", {{4, 1}, {-6, -1}},   {{-4, 6}, {-1, 2}},  {{-17, 26}, {25, -38}}},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        volatile int32_t A[2][2], B[2][2];
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) { A[i][j] = kRounds[r].A[i][j]; B[i][j] = kRounds[r].B[i][j]; }

        int32_t C[2][2];
        gemm_2x2(A, B, C);

        int ok = 1;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                if (C[i][j] != kRounds[r].C_expected[i][j]) ok = 0;
        all_ok = all_ok && ok;

        printf("%s: C=[[%d,%d],[%d,%d]] expected=[[%d,%d],[%d,%d]] -> %s\n",
               kRounds[r].label, C[0][0], C[0][1], C[1][0], C[1][1],
               kRounds[r].C_expected[0][0], kRounds[r].C_expected[0][1],
               kRounds[r].C_expected[1][0], kRounds[r].C_expected[1][1],
               ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
