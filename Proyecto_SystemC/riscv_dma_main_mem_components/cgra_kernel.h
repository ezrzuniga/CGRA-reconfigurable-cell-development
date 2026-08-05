#ifndef CGRA_KERNEL_H
#define CGRA_KERNEL_H

#include <cstdint>

enum CGRA_KERNEL
{
    VECTOR_ADD = 0,
    FIR_FILTER = 1,
    FFT_8_POINTS = 2,
    // e = (a + b) * 2, recorriendo las 4 celdas del arreglo CGRA 2x2 (Enrutamiento,
    // Memoria, Escalar, Vectorial) -- ver mesh_wrapper/mesh_wrapper.h,
    // PROGRAM_FULL_PIPELINE (mismo valor numerico a proposito).
    FULL_PIPELINE = 3,

    // Reduccion por suma: total = seed + sum(v[0..6]), recorriendo las 4 celdas.
    // Hoy solo se ejercita desde el FakeCsrDma de mesh_wrapper/, no desde este
    // CSR_DMA real -- la entrada existia en MeshProgram (PROGRAM_SUM_REDUCTION) pero
    // faltaba su contraparte aca, dejando un hueco en el valor 4 de este enum.
    SUM_REDUCTION = 4,

    // Multiplicacion matricial 2x2: C = A * B. A y B llegan como 8 int32
    // (row-major) en INPUT_DATA_BUFFER; C sale como 4 int32 (row-major) en los 4
    // lanes de OUTPUT_DATA_BUFFER. La computa entera la celda Vectorial (SIMD real
    // por lane) -- ver mesh_wrapper/mesh_wrapper.h, PROGRAM_MATMUL (mismo valor
    // numerico a proposito).
    MATMUL = 5,

    // e^u para los 4 lanes a la vez, en punto fijo Q4.12. Primer bloque
    // aritmetico no-polinomico del catalogo: el ISA no tiene exp ni division, asi
    // que se construye con range reduction base-2 (el 2^k sale de un OP_SRA con
    // shamt por-lane) mas un polinomio grado 3 para la mantisa. Pensado como pieza
    // reusable del softmax -- ver mesh_wrapper/mesh_wrapper.h, PROGRAM_EXP_VEC
    // (mismo valor numerico a proposito).
    EXP_VEC = 6,

    // Softmax sobre 4 lanes en punto fijo Q4.12: y_i = e^(x_i-max)/sum_j e^(x_j-max).
    // Primer kernel repartido entre DOS celdas por razones arquitectonicas: Vectorial
    // hace todo lo lane-parallel (reducciones por butterfly, exponenciales,
    // normalizacion) y Escalar el reciproco 1/S por Newton-Raphson -- ver
    // mesh_wrapper/mesh_wrapper.h, PROGRAM_SOFTMAX (mismo valor numerico a proposito).
    SOFTMAX = 7
};

//======================================================
// Forma de los buffers por kernel
//======================================================
// Todo kernel implementado por MeshWrapper usa el mismo tamano fijo, impuesto por el
// mapa de registros del lado CGRA y no por data_size: INPUT_DATA_BUFFER son siempre
// 32 bytes y OUTPUT_DATA_BUFFER siempre 16 (mesh_wrapper.cpp aborta con
// SC_REPORT_ERROR si recibe otra cosa). data_size solo aplica a los kernels del enum
// que todavia NO tienen implementacion en la malla (FIR_FILTER, FFT_8_POINTS), que
// siguen el camino generico byte a byte.
//
// Esta funcion existe para tener UN solo lugar donde decir eso. Antes la misma
// condicion estaba replicada en 6 sitios (4 en csr_dma.cpp, 2 en riscv_core.cpp) como
// un ternario con la lista de kernels escrita a mano: agregar un kernel obligaba a
// acordarse de los 6, y olvidarse de uno solo se manifiesta como un buffer truncado
// en tiempo de simulacion, no como un error de compilacion.
//
// SUM_REDUCTION esta incluido aunque hoy solo se ejercite via el FakeCsrDma de
// mesh_wrapper/ (nunca por este CSR_DMA real): tambien es 32/16 del lado CGRA, asi
// que dejarlo afuera seria plantar el mismo bug para quien le agregue un test
// end-to-end mas adelante.
inline bool cgra_kernel_uses_fixed_vector_buffers(uint32_t kernel)
{
    return kernel == VECTOR_ADD
        || kernel == FULL_PIPELINE
        || kernel == SUM_REDUCTION
        || kernel == MATMUL
        || kernel == EXP_VEC
        || kernel == SOFTMAX;
}

static const uint32_t CGRA_VECTOR_INPUT_BYTES  = 32;  // 8 int32
static const uint32_t CGRA_VECTOR_OUTPUT_BYTES = 16;  // 4 lanes int32

#endif
