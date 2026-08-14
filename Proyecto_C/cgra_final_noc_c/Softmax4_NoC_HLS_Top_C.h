// Softmax4_NoC_HLS_Top_C.h
// Top de Vitis HLS que sintetiza la CGRA_Final_NoC_Mesh_C real ejecutando el
// softmax base-2 de 4 elementos de CGRA_Final_Softmax4_C__TB.cpp: la malla
// computa EXP2(x_i)=1<<(x_i+9) (exacto, sin punto flotante ni serie
// truncada -- ver comentario de cabecera del TB de referencia) y la reduccion
// SUM=sum(EXP2(x_i)); la division final softmax_i = EXP2(x_i)/SUM es la parte
// ESCALAR que un acelerador real deja fuera del array, para el host -- por
// eso este top expone SUM y e0 (mismo contrato de salida que el TB de
// referencia, que solo valida esos dos valores), no el vector softmax
// completo.
//
// static Mesh mesh persistente, programa cargado una unica vez, 12 ciclos de
// malla por invocacion (1 de armado + 11 de ejecucion).

#ifndef SOFTMAX4_NOC_HLS_TOP_C_H
#define SOFTMAX4_NOC_HLS_TOP_C_H

#include <cstdint>
#include "CGRA_Final_NoC_Mesh_C.h"

void Softmax4_NoC_HLS_Top_C(int32_t x0, int32_t x1, int32_t x2, int32_t x3,
                             int32_t *sum_out, int32_t *e0_out);

#endif // SOFTMAX4_NOC_HLS_TOP_C_H
