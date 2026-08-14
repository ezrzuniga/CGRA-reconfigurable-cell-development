// FFT4_NoC_HLS_Top_C.h
// Top de Vitis HLS que sintetiza la CGRA_Final_NoC_Mesh_C real ejecutando la
// FFT de 4 puntos (radix-2, decimacion en frecuencia) de
// CGRA_Final_FFT4_C__TB.cpp: el bloque MAC sistolico 2x2 se reprograma como
// motor generico ADD/SUB (mariposas), y los factores twiddle W4^k (siempre
// +-1 o +-j) se resuelven en el EMPAQUETADO DE LANES de la entrada (ver
// comentario de cabecera de CGRA_Final_FFT4_C__TB.cpp), no con OP_MUL.
//
// Puertos: 4 complejos enteros de entrada (re,im) y 4 de salida -- mismo
// contrato que el testbench de referencia. static Mesh mesh persistente,
// programa cargado una unica vez, 12 ciclos de malla por invocacion (1 de
// armado + 11 de ejecucion).

#ifndef FFT4_NOC_HLS_TOP_C_H
#define FFT4_NOC_HLS_TOP_C_H

#include <cstdint>
#include "CGRA_Final_NoC_Mesh_C.h"

void FFT4_NoC_HLS_Top_C(
    int32_t x0_re, int32_t x0_im, int32_t x1_re, int32_t x1_im,
    int32_t x2_re, int32_t x2_im, int32_t x3_re, int32_t x3_im,
    int32_t *X0_re, int32_t *X0_im, int32_t *X1_re, int32_t *X1_im,
    int32_t *X2_re, int32_t *X2_im, int32_t *X3_re, int32_t *X3_im);

#endif // FFT4_NOC_HLS_TOP_C_H
