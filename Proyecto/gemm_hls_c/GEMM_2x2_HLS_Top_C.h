// GEMM_2x2_HLS_Top_C.h
// Wrapper delgado que instancia el template generico cgra_run<...>
// (cgra_hls_c/CGRA_Top_C.h) con los parametros de GEMM 2x2 (2x2 PE_MAC,
// DATA_W=32, VLEN=1, 8 registros, 4 slots de instr_mem, 2 fases). Es
// literalmente "sacar una CGRA sintetizable especifica del template": toda
// la FSM de fases, la interfaz de programacion y el wiring de bordes viven
// en cgra_hls_c/CGRA_Top_C.h -- este archivo solo aporta los tamanos y el
// unico estado con memoria del diseno (`static GemmMesh_C mesh`, ver el
// .cpp), que es lo que hace a esta CGRA reprogramable en vez de tener un
// programa fijo en el hardware: el host sube instrucciones con prog_valid/
// prog_row/prog_col/prog_slot/prog_instr, y puede disparar start muchas
// veces (con operandos distintos) sin volver a programar.
//
// Puertos de borde (in_N/in_S/in_W/in_E, out_N/out_S/out_W/out_E) expuestos
// genericamente, dimensionados por GEMM_ROWS/GEMM_COLS igual que el resto de
// la malla -- GEMM 2x2 solo usa in_W/in_N como entrada (A por columna oeste,
// B por fila norte) y out_W/out_E como salida (C), pero in_S/in_E/out_N/
// out_S quedan expuestos igual (en cero) por fidelidad al template.
//
// Declaracion separada de la definicion (en el .cpp) a proposito: Vitis HLS
// no encuentra un top marcado `inline` ("ERROR: [HLS 214-157] Top function
// not found") cuando el .cpp del diseno solo hace #include de un header con
// el cuerpo completo -- csynth_design necesita una definicion real, no
// inline, en la unidad de traduccion del diseno.

#ifndef GEMM_2X2_HLS_TOP_C_H
#define GEMM_2X2_HLS_TOP_C_H

#include "GEMM_2x2_Mesh_C.h"

void GEMM_2x2_HLS_Top_C(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    GemmInstr_C prog_instr,
    bool start, bool& done,
    GemmLink_C in_N[GEMM_NUM_PHASES][GEMM_COLS], GemmLink_C in_S[GEMM_NUM_PHASES][GEMM_COLS],
    GemmLink_C in_W[GEMM_NUM_PHASES][GEMM_ROWS], GemmLink_C in_E[GEMM_NUM_PHASES][GEMM_ROWS],
    GemmLink_C out_N[GEMM_COLS], GemmLink_C out_S[GEMM_COLS],
    GemmLink_C out_W[GEMM_ROWS], GemmLink_C out_E[GEMM_ROWS]);

#endif // GEMM_2X2_HLS_TOP_C_H
