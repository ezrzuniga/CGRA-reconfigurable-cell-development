#ifndef CGRA_KERNEL_H
#define CGRA_KERNEL_H

enum CGRA_KERNEL
{
    VECTOR_ADD = 0,
    FIR_FILTER = 1,
    FFT_8_POINTS = 2,
    // e = (a + b) * 2, recorriendo las 4 celdas del arreglo CGRA 2x2 (Enrutamiento,
    // Memoria, Escalar, Vectorial) -- ver mesh_wrapper/mesh_wrapper.h,
    // PROGRAM_FULL_PIPELINE (mismo valor numerico a proposito).
    FULL_PIPELINE = 3
};

#endif
