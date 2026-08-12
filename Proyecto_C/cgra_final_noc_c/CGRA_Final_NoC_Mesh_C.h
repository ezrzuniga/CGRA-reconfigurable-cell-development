// CGRA_Final_NoC_Mesh_C.h
// Transliteracion a C/C++ puro de cgra_final_noc/CGRA_Final_NoC_Mesh.h:
// version Network-on-Chip de cgra_final_c/CGRA_Final_Mesh_C.h. EXACTAMENTE el
// mismo layout 3x3, las mismas 5 variantes de celda en las mismas 9
// posiciones (nada en pe_hls_c/memory_hls_c/mesh_hls_c se toca ni se
// modifica), pero instanciadas sobre NoC_Mesh_Static_C en vez de sobre
// CGRA_Mesh_Static_C: la interconexion entre celdas pasa de wires punto a
// punto a una malla de routers que reenvian paquetes con header
// (dest_row,dest_col) -- ver NoC_Packet_C.h para que es un NoC y en que se
// diferencia de la malla directa.
//
//   (0,0) Memoria     (0,1) Vectorial   (0,2) Escalar
//   (1,0) Routing     (1,1) MAC         (1,2) MAC
//   (2,0) Routing     (2,1) MAC         (2,2) MAC
//
// Mismos bordes reales, mismas 5 celdas con el mismo comportamiento interno --
// ver cgra_final_c/CGRA_Final_Mesh_C.h para el mapa completo de bordes reales
// por celda, que no cambia aca. La unica diferencia observable desde afuera es
// estructural (paquetes en vez de wires fijos); el comportamiento ciclo a
// ciclo es identico -- confirmado en CGRA_Final_NoC_Mesh_C__TB.cpp y
// CGRA_Final_NoC_GEMM_C__TB.cpp reusando exactamente los mismos vectores de
// prueba y el mismo programa espacial que sus equivalentes de malla directa.
//
// Igual que CGRA_Final_Mesh_C, esta malla NO trae un programa espacial
// pre-cargado: se programa despues via noc_mesh_program().

#ifndef CGRA_FINAL_NOC_MESH_C_H
#define CGRA_FINAL_NOC_MESH_C_H

#include "NoC_Mesh_Static_C.h"
#include "../cgra_final_c/CGRA_Final_Mesh_C.h"  // reusa el layout y los typedefs de celda

typedef NoC_Mesh_Static_C<CGRA_FINAL_ROWS, CGRA_FINAL_COLS, CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN,
    CGRA_Final_MemCell,   // (0,0)
    CGRA_Final_VecCell,   // (0,1)
    CGRA_Final_ScaCell,   // (0,2)
    CGRA_Final_RouCell,   // (1,0)
    CGRA_Final_MacCell,   // (1,1)
    CGRA_Final_MacCell,   // (1,2)
    CGRA_Final_RouCell,   // (2,0)
    CGRA_Final_MacCell,   // (2,1)
    CGRA_Final_MacCell>   // (2,2)
    CGRA_Final_NoC_Mesh_C;

typedef CGRA_Final_NoC_Mesh_C::Link   CGRA_Final_NoC_Link;
typedef CGRA_Final_NoC_Mesh_C::Instr  CGRA_Final_NoC_Instr;
typedef CGRA_Final_NoC_Mesh_C::Packet CGRA_Final_NoC_Packet;

// Envoltorio delgado, espejo de cgra_final_step() -- misma justificacion del
// PIPELINE II=1: las 9 celdas + los 9 routers de un mismo ciclo forman una
// unica region de datapath, sin dependencias hacia atras dentro del ciclo.
inline void cgra_final_noc_step(CGRA_Final_NoC_Mesh_C& mesh, bool rst, bool enable,
                                 const CGRA_Final_NoC_Link in_N[CGRA_FINAL_COLS],
                                 const CGRA_Final_NoC_Link in_S[CGRA_FINAL_COLS],
                                 const CGRA_Final_NoC_Link in_W[CGRA_FINAL_ROWS],
                                 const CGRA_Final_NoC_Link in_E[CGRA_FINAL_ROWS],
                                 CGRA_Final_NoC_Link out_N[CGRA_FINAL_COLS],
                                 CGRA_Final_NoC_Link out_S[CGRA_FINAL_COLS],
                                 CGRA_Final_NoC_Link out_W[CGRA_FINAL_ROWS],
                                 CGRA_Final_NoC_Link out_E[CGRA_FINAL_ROWS])
{
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
    noc_mesh_step(mesh, rst, enable, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}

#endif // CGRA_FINAL_NOC_MESH_C_H
