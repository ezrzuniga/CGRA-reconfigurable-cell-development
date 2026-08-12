// GEMM_2x2_Scalar_Top_C.h
// Transliteracion a C/C++ puro de la INTERFAZ de gemm_hls/GEMM_2x2_HLS_Top.h:
// un acelerador GEMM 2x2 de funcion fija detras de un limite de puertos
// puramente escalares (A y B completas entran como 8 enteros, C sale como 4),
// mas start/done. Ningun puerto de este top usa Link/PE_VectorData ni arreglos
// de fase -- exactamente el mismo criterio del original: no exponer un tipo
// compuesto en el limite de sintesis, para que envolverlo en AXI-Lite desde el
// host sea trivial.
//
// Por que existe ADEMAS de GEMM_2x2_HLS_Top_C: son dos tops distintos con dos
// contratos distintos, y el port necesitaba los dos.
//
//   GEMM_2x2_HLS_Top_C   (GEMM_2x2_HLS_Top_C.h)
//     CGRA REPROGRAMABLE. Expone el canal de programacion (prog_valid/
//     prog_row/prog_col/prog_slot/prog_instr) y bordes genericos por fase. El
//     host sube el programa espacial que quiera -- GEMM es solo uno posible --
//     y despues dispara corridas. Es el que "sale" del template generico
//     cgra_hls_c/CGRA_Top_C.h y no tiene equivalente en el arbol SystemC.
//
//   GEMM_2x2_Scalar_Top_C (este archivo)
//     ACELERADOR DE FUNCION FIJA. El programa espacial de GEMM esta cableado
//     adentro (se carga en instr_mem en cada invocacion, canal lateral, 0
//     ciclos); el host solo ve numeros. Es el equivalente 1:1 del top SystemC.
//
// Diferencias de mecanica respecto del original, ninguna observable desde el
// host:
//   - La FSM de 10 estados del original (BOOT_LOAD -> BOOT_CLEAR -> IDLE ->
//     CLEAR_ACC -> REALIGN -> RELOAD -> PHASE0 -> PHASE1 -> WAIT_DONE -> DONE)
//     se reduce a la FSM generica de cgra_run<...>. Los 4 estados que el
//     original dedicaba a cargar/limpiar (BOOT_LOAD, BOOT_CLEAR, CLEAR_ACC,
//     RELOAD) desaparecen: en C cargar instr_mem y limpiar el acumulador son
//     canales laterales directos (mesh_program/mesh_clear_acc), no ciclos de
//     ejecucion pisando instr_mem.
//   - Los "adelantos de un ciclo" de RELOAD/PHASE0 que el original necesitaba
//     (porque escribir hacia la malla desde un SC_METHOD sincrono se veia un
//     ciclo despues) siguen existiendo, pero ya viven una sola vez dentro de
//     cgra_run (el registro MeshDrive curr/nxt), no repetidos por estado.
//   - No hay latch de A/B: el original los copiaba a ra00..rb11 al aceptar
//     start para que el host pudiera cambiar los puertos durante la corrida.
//     Aca una invocacion del top ES la corrida completa, asi que los
//     argumentos son estables por construccion y el latch no aplica.
//
// Igual que el original, cada invocacion es independiente del historial:
// cgra_run limpia los acumuladores antes de la fase 0, asi que el mismo A/B da
// el mismo C sin importar cuantas corridas hubo antes.

#ifndef GEMM_2X2_SCALAR_TOP_C_H
#define GEMM_2X2_SCALAR_TOP_C_H

#include "GEMM_2x2_Mesh_C.h"

void GEMM_2x2_Scalar_Top_C(
    bool start, bool& done,
    ap_int<GEMM_DATA_W> a00, ap_int<GEMM_DATA_W> a01,
    ap_int<GEMM_DATA_W> a10, ap_int<GEMM_DATA_W> a11,
    ap_int<GEMM_DATA_W> b00, ap_int<GEMM_DATA_W> b01,
    ap_int<GEMM_DATA_W> b10, ap_int<GEMM_DATA_W> b11,
    ap_int<GEMM_DATA_W>& c00, ap_int<GEMM_DATA_W>& c01,
    ap_int<GEMM_DATA_W>& c10, ap_int<GEMM_DATA_W>& c11);

#endif // GEMM_2X2_SCALAR_TOP_C_H
