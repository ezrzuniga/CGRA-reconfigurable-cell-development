// GEMM_NoC_HLS_Top_C.h
// Top de Vitis HLS que sintetiza la CGRA_Final_NoC_Mesh_C REAL (malla 3x3
// heterogenea -- Memoria/Vectorial/Escalar/Routing/MAC -- interconectada por
// la fabrica de routers de NoC_Mesh_Static_C.h) ejecutando el mismo programa
// espacial de GEMM 2x2 que CGRA_Final_NoC_GEMM_C__TB.cpp ya valida: bloque
// MAC sistolico 2x2 embebido en la malla (celdas (1,1)/(1,2)/(2,1)/(2,2)),
// con Vectorial/Escalar relevando B por la columna 1/2 y Routing(1,0)/(2,0)
// conmutando entre "traer A al bloque" (ctx0) y "sacar C al borde" (ctx1).
//
// A diferencia de sum_reduction_kernel (Proyecto_HLS/hls_vitis_sum_reduction),
// que solo reproduce el contrato aritmetico, este top NO reimplementa la
// multiplicacion de matrices en C++: la calcula la malla, instruccion por
// instruccion, ciclo por ciclo, paquete de router por paquete de router --
// las mismas 9 celdas y el mismo programa que ya corren bajo csim/cosim en
// CGRA_Final_NoC_GEMM_C__TB.cpp, ahora detras de una unica funcion top
// sintetizable en vez de un main() imperativo.
//
// static Mesh mesh (ver el .cpp): el programa espacial se carga una UNICA vez
// (primera invocacion) y persiste entre llamadas -- CGRA reconfigurable de
// verdad, no un programa fijo recompilado en cada corrida. Cada llamada
// posterior solo arma el caso (reset + routing de entrada), alimenta A/B y
// lee C -- 11 ciclos de malla por invocacion (1 de armado + 4+4 de
// alimentacion k=0/k=1 + 2 de lectura de acumuladores).

#ifndef GEMM_NOC_HLS_TOP_C_H
#define GEMM_NOC_HLS_TOP_C_H

#include <cstdint>
#include "CGRA_Final_NoC_Mesh_C.h"

void GEMM_NoC_HLS_Top_C(const int32_t A[2][2], const int32_t B[2][2], int32_t C[2][2]);

#endif // GEMM_NOC_HLS_TOP_C_H
