// CGRA_Final_Mesh_C.h
// Transliteracion a C/C++ puro (sintetizable por Vitis HLS, sin SystemC) de
// cgra_final/CGRA_Final_Mesh.h: la malla final de la CGRA, layout fijo 3x3
// con los 5 tipos de celda, instanciada sobre CGRA_Mesh_Static_C
// (mesh_hls_c/CGRA_Mesh_Static_C.h) en vez de sobre CGRA_Mesh_Static.
//
// Layout row-major (indice = fila*3+columna), fijado por el orden de los
// argumentos del template -- identico al original:
//
//   (0,0) Memoria     (0,1) Vectorial   (0,2) Escalar
//   (1,0) Routing     (1,1) MAC         (1,2) MAC
//   (2,0) Routing     (2,1) MAC         (2,2) MAC
//
// Bordes reales de la malla (fila 0 = N, fila ROWS-1 = S, columna 0 = W,
// columna COLS-1 = E):
//   Memoria  (0,0): N, W  -- su unico puerto con logica real es W (ver
//     memory_hls_c/PE_Memory_HLS_C.h), que coincide con el borde real: es el
//     gateway directo de la malla hacia el sistema externo.
//   Vectorial(0,1): N     -- alimenta el operando "norte" de la columna 1.
//   Escalar  (0,2): N, E  -- alimenta el operando "norte" de la columna 2.
//   Routing  (1,0): W     -- relevo del operando "oeste" hacia la fila 1.
//   Routing  (2,0): S, W  -- idem para la fila 2.
//   MAC (1,1)(1,2)(2,1)(2,2): bloque sistolico 2x2, mismo shape que
//     gemm_hls_c/GEMM_2x2_Mesh_C.h.
//
// UNICA diferencia de comportamiento respecto del original SystemC, y hay que
// tenerla presente al portar cualquier programa espacial (por eso esta
// documentada aca y no en cada testbench): en la malla SystemC la celda de
// Routing era un mux COMBINACIONAL (relevo de 0 ciclos) mientras que las
// celdas tipo PE tenian salida registrada (1 ciclo). En C esa asimetria
// desaparece: mesh_step() toma un snapshot de las salidas de TODAS las celdas
// antes de correr a ninguna, asi que absolutamente todo salto celda-a-celda
// cuesta exactamente 1 ciclo, Routing incluido. A cambio, un borde externo
// (bound_in_*) SI se ve en el mismo ciclo en que se escribe -- en SystemC un
// sc_signal escrito desde sc_main recien se aplicaba en el sc_start()
// siguiente. Resultado neto para un programa espacial: el camino
// "borde -> Routing -> PE" cuesta 1 ciclo aca igual que alla (0+1 vs 1+0), y
// el camino "borde -> Vectorial/Escalar -> PE" pasa de 2 ciclos a 1 -- o sea
// que en C los operandos que entran por Routing y los que entran por
// Vectorial/Escalar quedan ALINEADOS, cosa que en SystemC no pasaba. Los
// programas de cgra_final_TB_c/ aprovechan ese alineamiento y por eso su
// calendario de slots es mas corto que el del original.
//
// Igual que el original, esta malla NO trae un programa espacial pre-cargado:
// es de proposito general, el algoritmo (GEMM/FFT/SoftMax/reduccion/...) se
// programa despues via mesh_program(), como cualquier CGRA_Mesh_Static_C.

#ifndef CGRA_FINAL_MESH_C_H
#define CGRA_FINAL_MESH_C_H

#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"
#include "../memory_hls_c/PE_Memory_HLS_C.h"
#include "../pe_hls_c/vector/PE_Vector_HLS_C.h"
#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../pe_hls_c/routing/Routing_Cell_HLS_C.h"
#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"

static const int CGRA_FINAL_ROWS = 3;
static const int CGRA_FINAL_COLS = 3;
static const int CGRA_FINAL_DATA_W = 32;
static const int CGRA_FINAL_VLEN = 4;
static const int CGRA_FINAL_NUM_REGS = 8;
static const int CGRA_FINAL_INSTR_MEM_SIZE = 16;
static const int CGRA_FINAL_SRAM_WORDS = 512;

typedef PE_Memory_State<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_SRAM_WORDS>                       CGRA_Final_MemCell;
typedef PE_Vector_State<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE> CGRA_Final_VecCell;
typedef PE_Scalar_State<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE> CGRA_Final_ScaCell;
typedef Routing_Cell_State<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN>                                            CGRA_Final_RouCell;
typedef PE_MAC_State<CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN, CGRA_FINAL_NUM_REGS, CGRA_FINAL_INSTR_MEM_SIZE>    CGRA_Final_MacCell;

typedef CGRA_Mesh_Static_C<CGRA_FINAL_ROWS, CGRA_FINAL_COLS, CGRA_FINAL_DATA_W, CGRA_FINAL_VLEN,
    CGRA_Final_MemCell,   // (0,0)
    CGRA_Final_VecCell,   // (0,1)
    CGRA_Final_ScaCell,   // (0,2)
    CGRA_Final_RouCell,   // (1,0)
    CGRA_Final_MacCell,   // (1,1)
    CGRA_Final_MacCell,   // (1,2)
    CGRA_Final_RouCell,   // (2,0)
    CGRA_Final_MacCell,   // (2,1)
    CGRA_Final_MacCell>   // (2,2)
    CGRA_Final_Mesh_C;

typedef CGRA_Final_Mesh_C::Link  CGRA_Final_Link;
typedef CGRA_Final_Mesh_C::Instr CGRA_Final_Instr;

// Un ciclo de la malla final. Envoltorio delgado sobre mesh_step() que fija
// los tamanos de los 4 bordes (COLS para N/S, ROWS para W/E) -- pensado para
// ser el cuerpo de un top de Vitis HLS o el "tick" de un testbench.
//
// PIPELINE II=1: las 9 celdas de un mismo ciclo son independientes entre si
// (todas leen el snapshot viejo, ver CGRA_Mesh_Static_C.h), asi que el ciclo
// de malla completo es una unica region de datapath sin dependencias
// internas -- exactamente lo que un CGRA hace en hardware, las 9 PEs
// disparando en paralelo en el mismo flanco.
inline void cgra_final_step(CGRA_Final_Mesh_C& mesh, bool rst, bool enable,
                             const CGRA_Final_Link in_N[CGRA_FINAL_COLS],
                             const CGRA_Final_Link in_S[CGRA_FINAL_COLS],
                             const CGRA_Final_Link in_W[CGRA_FINAL_ROWS],
                             const CGRA_Final_Link in_E[CGRA_FINAL_ROWS])
{
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1
    mesh_step(mesh, rst, enable, in_N, in_S, in_W, in_E);
}

// Vuelca los 4 bordes reales de la malla (fila 0 = N, fila 2 = S, columna 0 =
// W, columna 2 = E) a arreglos planos indexables en runtime -- el equivalente
// de los puertos out_N/out_S/out_W/out_E del sc_module original, que en C no
// existen porque la "salida de la malla" es simplemente el campo out_* de la
// celda de borde correspondiente.
inline void cgra_final_read_edges(CGRA_Final_Mesh_C& mesh,
                                   CGRA_Final_Link out_N[CGRA_FINAL_COLS],
                                   CGRA_Final_Link out_S[CGRA_FINAL_COLS],
                                   CGRA_Final_Link out_W[CGRA_FINAL_ROWS],
                                   CGRA_Final_Link out_E[CGRA_FINAL_ROWS])
{
    CGRA_Final_Link all_N[CGRA_FINAL_ROWS][CGRA_FINAL_COLS], all_S[CGRA_FINAL_ROWS][CGRA_FINAL_COLS];
    CGRA_Final_Link all_E[CGRA_FINAL_ROWS][CGRA_FINAL_COLS], all_W[CGRA_FINAL_ROWS][CGRA_FINAL_COLS];
#pragma HLS ARRAY_PARTITION variable=all_N complete dim=0
#pragma HLS ARRAY_PARTITION variable=all_S complete dim=0
#pragma HLS ARRAY_PARTITION variable=all_E complete dim=0
#pragma HLS ARRAY_PARTITION variable=all_W complete dim=0
    mesh_read_outputs(mesh, all_N, all_S, all_E, all_W);

edge_cols_loop:
    for (int c = 0; c < CGRA_FINAL_COLS; c++) {
#pragma HLS UNROLL
        out_N[c] = all_N[0][c];
        out_S[c] = all_S[CGRA_FINAL_ROWS - 1][c];
    }
edge_rows_loop:
    for (int r = 0; r < CGRA_FINAL_ROWS; r++) {
#pragma HLS UNROLL
        out_W[r] = all_W[r][0];
        out_E[r] = all_E[r][CGRA_FINAL_COLS - 1];
    }
}

#endif // CGRA_FINAL_MESH_C_H
