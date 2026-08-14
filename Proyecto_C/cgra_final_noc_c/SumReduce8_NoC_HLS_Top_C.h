// SumReduce8_NoC_HLS_Top_C.h
// Top de Vitis HLS que sintetiza la CGRA_Final_NoC_Mesh_C real ejecutando el
// arbol de reduccion de 8 enteros de CGRA_Final_SumReduce8_C__TB.cpp (el
// puerto a C de cgra_final_TB/CGRA_Final_SumReduce8__TB.cpp): las 4 celdas
// MAC (P00 hub, P01, P10, P11) computan sumas reales -- no solo relevos --
// combinando acumulacion temporal (2 valores por borde, multiplexados en el
// tiempo) con reduccion espacial en arbol binario de 3 niveles. Ver el
// comentario de cabecera de CGRA_Final_SumReduce8_C__TB.cpp para el arbol
// completo y el mapeo fisico celda por celda; no se repite aca.
//
// Igual que GEMM_NoC_HLS_Top_C: static Mesh mesh persistente, programa
// espacial cargado una unica vez, 11 ciclos de malla por invocacion (1 de
// armado + 10 de ejecucion).

#ifndef SUM_REDUCE8_NOC_HLS_TOP_C_H
#define SUM_REDUCE8_NOC_HLS_TOP_C_H

#include <cstdint>
#include "CGRA_Final_NoC_Mesh_C.h"

void SumReduce8_NoC_HLS_Top_C(const int32_t v[8], int32_t *total);

#endif // SUM_REDUCE8_NOC_HLS_TOP_C_H
