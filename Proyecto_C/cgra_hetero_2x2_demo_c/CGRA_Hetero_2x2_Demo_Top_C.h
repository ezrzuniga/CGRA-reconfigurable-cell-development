// CGRA_Hetero_2x2_Demo_Top_C.h
// Top minimo de Vitis HLS para probar que la malla heterogenea 2x2
// (Routing + Memoria + Scalar + Vector, ver CGRA_Mesh_2x2_Heterogeneous_
// HLS_C__TB.cpp en mesh_hls_c/) sintetiza de verdad. No es una aplicacion
// especifica con FSM de fases como GEMM (cgra_run<...>/CGRA_Top_C.h siguen
// siendo eso): es la malla misma, expuesta con 2 caminos de control crudos
// -- programar una celda, o correr un ciclo -- para que un host (software o
// un testbench) orqueste la secuencia que su aplicacion necesite, igual que
// hace el testbench de referencia.
//
// Layout fijo (mismo que el testbench de referencia):
//   P00 = Routing_Cell    P01 = PE_Memory
//   P10 = PE_Scalar       P11 = PE_Vector
//
// Dos caminos, mutuamente excluyentes por llamada:
// 1) prog_valid=true: programa la celda (prog_row,prog_col) via
//    mesh_program() -- una instruccion/config por llamada.
// 2) prog_valid=false: corre un ciclo de mesh_step() con rst/enable y los 4
//    bordes de la malla como puertos, y vuelca los bordes de salida.
//
// static HeteroMesh_C mesh -- unico estado con memoria del diseno, persiste
// entre invocaciones (mismo patron que GEMM_2x2_HLS_Top_C).

#ifndef CGRA_HETERO_2X2_DEMO_TOP_C_H
#define CGRA_HETERO_2X2_DEMO_TOP_C_H

#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../pe_hls_c/vector/PE_Vector_HLS_C.h"
#include "../pe_hls_c/routing/Routing_Cell_HLS_C.h"
#include "../memory_hls_c/PE_Memory_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int HETERO_ROWS = 2;
static const int HETERO_COLS = 2;
static const int HETERO_DATA_W = 32;
static const int HETERO_VLEN = 1;
static const int HETERO_SIZE_WORDS = 64;

typedef Routing_Cell_State<HETERO_DATA_W, HETERO_VLEN>                     HeteroRoutingCell;
typedef PE_Memory_State<HETERO_DATA_W, HETERO_VLEN, HETERO_SIZE_WORDS>     HeteroMemoryCell;
typedef PE_Scalar_State<HETERO_DATA_W, HETERO_VLEN, 8, 4>                  HeteroScalarCell;
typedef PE_Vector_State<HETERO_DATA_W, HETERO_VLEN, 8, 4>                  HeteroVectorCell;

typedef CGRA_Mesh_Static_C<HETERO_ROWS, HETERO_COLS, HETERO_DATA_W, HETERO_VLEN,
                            HeteroRoutingCell, HeteroMemoryCell, HeteroScalarCell, HeteroVectorCell>
        HeteroMesh_C;
typedef HeteroMesh_C::Link  HeteroLink_C;
typedef HeteroMesh_C::Instr HeteroInstr_C;

void CGRA_Hetero_2x2_Demo_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot, HeteroInstr_C prog_instr,
    bool rst, bool enable,
    HeteroLink_C in_N[HETERO_COLS], HeteroLink_C in_S[HETERO_COLS],
    HeteroLink_C in_W[HETERO_ROWS], HeteroLink_C in_E[HETERO_ROWS],
    HeteroLink_C out_N[HETERO_COLS], HeteroLink_C out_S[HETERO_COLS],
    HeteroLink_C out_W[HETERO_ROWS], HeteroLink_C out_E[HETERO_ROWS]);

#endif // CGRA_HETERO_2X2_DEMO_TOP_C_H
