// GEMM_2x2_HLS_Top_C.h
// Top sintetizable para Vitis HLS en C/C++ puro: transliteracion de
// gemm_hls/GEMM_2x2_HLS_Top.h, que Vitis HLS 2024.1 rechaza con
// "ERROR: [HLS 200-637] SystemC input is not supported" por estar escrito
// como sc_module (ver Proyecto_HLS/hls_vitis_gemm_2x2_cgra/vitis_hls.log).
// Envuelve la misma malla real (CGRA_Mesh_Static_C<2,2,32,1, 4 celdas
// PE_MAC>, sin modificarla) con la misma FSM de fases, adaptada a un
// invariante distinto: en vez de un sc_module persistente que arranca una
// vez (BOOT_LOAD/BOOT_CLEAR) y despues espera en IDLE entre llamadas
// separadas, cada invocacion de esta funcion corre la secuencia completa
// (limpiar acumuladores, recargar el programa, fase 0, fase 1, lectura) de
// principio a fin y retorna con done=true -- start/done quedan como pulso
// de control de una sola llamada, no como handshake entre invocaciones (ver
// plan de migracion).
//
// Por que se puede saltar BOOT_LOAD/BOOT_CLEAR sin cambiar el resultado:
// esos dos estados solo existian para dejar *algo* cargado en instr_mem
// antes del primer `start` de un modulo persistente. CLEAR_ACC siempre
// sobreescribe las 4 direcciones con la instruccion de limpieza sin
// importar que hubiera antes, y RELOAD vuelve a cargar el programa real en
// esas mismas 4 direcciones -- por lo que partir de instr_mem vacio
// (PE_MAC_HLS_C ya lo inicializa a NOP) y arrancar directo en CLEAR_ACC da
// exactamente el mismo resultado que arrancar en BOOT_LOAD, un ciclo mas
// tarde.
//
// Disciplina de registro preservada de GEMM_2x2_HLS_Top.h: todo lo que la
// FSM le "presenta" a la malla en un ciclo (reset, carga de instruccion,
// operandos de borde in_W/in_N) solo se vuelve efectivo para la malla en el
// ciclo SIGUIENTE -- la malla, dentro de esta misma funcion, es otro bloque
// de hardware sincrono separado, no una lectura combinacional del mismo
// ciclo. Se modela con un registro explicito (MeshDrive `curr`/`nxt`, en
// GEMM_2x2_HLS_Top_C.cpp) que por defecto es "pegajoso" (retiene el valor
// del ciclo anterior salvo que el estado actual lo sobreescriba), igual que
// un sc_signal que no recibe write() ese ciclo. Ver GEMM_2x2_HLS_Top.h para
// la "Nota de temporizacion" original y los mismos casos de prueba en el
// testbench.
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
    bool start, bool& done,
    ap_int<32> a00, ap_int<32> a01, ap_int<32> a10, ap_int<32> a11,
    ap_int<32> b00, ap_int<32> b01, ap_int<32> b10, ap_int<32> b11,
    ap_int<32>& c00, ap_int<32>& c01, ap_int<32>& c10, ap_int<32>& c11);

#endif // GEMM_2X2_HLS_TOP_C_H
