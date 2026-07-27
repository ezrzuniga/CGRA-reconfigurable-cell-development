// sum_reduction_workload.c
// Reimplementacion standalone (sin SystemC, sin pragmas HLS) del contrato
// aritmetico de PROGRAM_SUM_REDUCTION: total = seed + sum(v[0..6]). Mismos
// valores dorados que MeshWrapper_SumReduction__TB.cpp y
// Proyecto_HLS/hls_vitis_sum_reduction/tb_sum_reduction.cpp, para que las 3
// implementaciones (CGRA SystemC, kernel HLS, este binario) sean
// comparables entre si.
//
// Pensado para correr bajo gem5 en modo SE (syscall emulation): un binario
// nativo normal, sin threads ni syscalls raras, que gem5 ejecuta
// instruccion por instruccion sobre el modelo de CPU que elijas
// (AtomicSimpleCPU, TimingSimpleCPU, DerivO3CPU, etc.) para medir ciclos.
//
// Compilar (nativo x86_64, estatico para que gem5 no dependa del linker
// dinamico del sistema en SE mode):
//   gcc -O0 -static -o sum_reduction_workload sum_reduction_workload.c
//
// -O0 a proposito: si el compilador optimiza el loop de reduccion (lo puede
// hacer trivialmente, es una suma de 7 constantes), el conteo de ciclos que
// gem5 mide deja de reflejar "ejecutar el kernel real" y pasa a medir
// "ejecutar un resultado precomputado". Para comparar arquitecturas de CPU
// (in-order vs out-of-order, por ejemplo) sobre el MISMO trabajo, conviene
// desactivar la optimizacion que colapsa el kernel a una constante.

#include <stdio.h>
#include <stdint.h>

#define SUM_REDUCTION_VECTOR_LEN 7

// volatile: refuerza a -O0 que no debe pre-calcular nada en tiempo de
// compilacion (defensivo, redundante con -O0 pero documenta la intencion).
static int32_t sum_reduction_kernel(int32_t seed, volatile int32_t *v) {
    int32_t total = seed;
    for (int i = 0; i < SUM_REDUCTION_VECTOR_LEN; i++) {
        total += v[i];
    }
    return total;
}

typedef struct {
    const char *label;
    int32_t seed;
    int32_t v[SUM_REDUCTION_VECTOR_LEN];
    int32_t expected_total;
} Round;

static const Round kRounds[] = {
    {"Ronda 1", 100, {6, -2, 9, 4, 0, 7, -5}, 119},
    {"Ronda 2", 0,   {1, 2, 3, 4, 5, 6, 7},   28},
    {"Ronda 3", -50, {10, -20, 30, -40, 50, -60, 70}, -10},
};

int main(void) {
    int all_ok = 1;
    int num_rounds = (int)(sizeof(kRounds) / sizeof(kRounds[0]));

    for (int r = 0; r < num_rounds; r++) {
        volatile int32_t v[SUM_REDUCTION_VECTOR_LEN];
        for (int i = 0; i < SUM_REDUCTION_VECTOR_LEN; i++) v[i] = kRounds[r].v[i];

        int32_t total = sum_reduction_kernel(kRounds[r].seed, v);
        int ok = (total == kRounds[r].expected_total);
        all_ok = all_ok && ok;

        printf("%s: seed=%d total=%d expected=%d -> %s\n",
               kRounds[r].label, kRounds[r].seed, total,
               kRounds[r].expected_total, ok ? "PASS" : "FAIL");
    }

    printf(all_ok ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return all_ok ? 0 : 1;
}
