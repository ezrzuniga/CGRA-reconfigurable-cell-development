// CGRA_Final_Mesh.h
// Malla final de la CGRA: layout fijo 3x3 con los 5 tipos de celda
// sintetizables (Memoria, Vectorial, Escalar, Routing, MAC), lista para
// instanciar en testbenches, simulaciones o el flujo de implementacion FPGA.
//
// Layout row-major (indice = fila*3+columna), fijado por el orden de los
// argumentos del template de CGRA_Mesh_Static (mesh_hls/CGRA_Mesh_Static.h):
//
//   (0,0) Memoria     (0,1) Vectorial   (0,2) Escalar
//   (1,0) Routing     (1,1) MAC         (1,2) MAC
//   (2,0) Routing     (2,1) MAC         (2,2) MAC
//
// Bordes reales de la malla (fila 0 = N, fila ROWS-1 = S, columna 0 = W,
// columna COLS-1 = E):
//   Memoria  (0,0): N, W  -- unico puerto con logica real es W (ver
//     memory_hls/PE_Memory_HLS_Cell.h); su W coincide con el borde real, asi
//     que no necesita ninguna celda de Routing como relevo -- es el gateway
//     directo de la malla hacia el sistema externo.
//   Vectorial(0,1): N     -- alimenta el operando "norte" de la columna 1
//     del bloque MAC.
//   Escalar  (0,2): N, E  -- alimenta el operando "norte" de la columna 2
//     del bloque MAC.
//   Routing  (1,0): W     -- relevo combinacional (0 ciclos) del operando
//     "oeste" hacia la fila 1 del bloque MAC.
//   Routing  (2,0): S, W  -- idem para la fila 2; con dos bordes reales, la
//     celda mas facil de ejercitar en aislamiento.
//   MAC (1,1)(1,2)(2,1)(2,2): bloque sistolico 2x2, mismo shape que
//     gemm_hls/GEMM_2x2_Mesh.h (reusable tal cual para GEMM 2x2; MAC(2,1)/
//     MAC(2,2) ademas tienen borde real S, y MAC(1,2)/MAC(2,2) borde real E).
//
// A diferencia de gemm_hls/GEMM_2x2_Mesh.h, esta malla NO trae un programa
// espacial pre-cargado: es de proposito general, el algoritmo (GEMM/FIR/
// FFT/SoftMax/...) se programa despues via mesh.load_instr()/
// mesh.clear_instr(), como cualquier CGRA_Mesh_Static.

#ifndef CGRA_FINAL_MESH_H
#define CGRA_FINAL_MESH_H

#include "../mesh_hls/CGRA_Mesh_Static.h"
#include "../memory_hls/PE_Memory_HLS_Cell.h"
#include "../pe_hls/vector/PE_Vector_Cell_HLS.h"
#include "../pe_hls/scalar/PE_Scalar_Cell_HLS.h"
#include "../pe_hls/routing/PE_Routing_Cell_HLS.h"
#include "../pe_hls/mac/PE_MAC_Cell_HLS.h"

static const int CGRA_FINAL_ROWS = 3;
static const int CGRA_FINAL_COLS = 3;
static const int CGRA_FINAL_DATA_W = 32;
static const int CGRA_FINAL_VLEN = 4;
static const int CGRA_FINAL_NUM_REGS = 8;
static const int CGRA_FINAL_INSTR_MEM_SIZE = 16;

typedef CGRA_Mesh_Static<CGRA_FINAL_ROWS, CGRA_FINAL_COLS, CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN,
    PE_Memory_HLS_Cell<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN>,                                                  // (0,0)
    PE_Vector_Cell_HLS<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE>,  // (0,1)
    PE_Scalar_Cell_HLS<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE>,  // (0,2)
    PE_Routing_Cell_HLS<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN>,                                                 // (1,0)
    PE_MAC_Cell_HLS<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE>,     // (1,1)
    PE_MAC_Cell_HLS<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE>,     // (1,2)
    PE_Routing_Cell_HLS<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN>,                                                 // (2,0)
    PE_MAC_Cell_HLS<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE>,     // (2,1)
    PE_MAC_Cell_HLS<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE>>     // (2,2)
    CGRA_Final_Mesh;

typedef CGRA_Final_Mesh::Link    CGRA_Final_Link;
typedef CGRA_Final_Mesh::Instr   CGRA_Final_Instr;
typedef CGRA_Final_Mesh::InstrIn CGRA_Final_InstrIn;

#endif // CGRA_FINAL_MESH_H
