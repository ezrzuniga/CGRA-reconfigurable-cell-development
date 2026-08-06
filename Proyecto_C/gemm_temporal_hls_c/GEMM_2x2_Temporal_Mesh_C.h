// GEMM_2x2_Temporal_Mesh_C.h
// Mapeo TEMPORAL de GEMM 2x2 sobre el template generico cgra_run<...>: una
// unica celda PE_MAC (ROWS=1,COLS=1) reutilizada para calcular los 4
// elementos de salida C[i][j] UNO A LA VEZ, en 4 invocaciones separadas del
// top -- contraparte de gemm_hls_c/GEMM_2x2_Mesh_C.h (4 PE_MAC en 2x2, los 4
// elementos de salida se calculan en paralelo, una celda fisica por salida).
//
// Cada invocacion de cgra_run computa un solo C[i][j] = A[i][0]*B[0][j] +
// A[i][1]*B[1][j] con NUM_PHASES=2 (una fase por termino k=0,1): la fase k
// presenta A[i][k] en in_N y B[k][j] en in_S, y la instruccion residente
// hace acc += A[i][k]*B[k][j] (OP_MAC), exportando a out_E. Como cgra_run
// llama mesh_clear_acc() al inicio de CADA invocacion con start=true (ver
// cgra_hls_c/CGRA_Top_C.h), cada una de las 4 corridas parte de acc=0 sin
// necesidad de instruccion de "seed" -- el host simplemente invoca el top 4
// veces (una por posicion de salida), leyendo out_E despues de cada una.
//
// Igual que sum_reduction_hls_c/SumReduction_Temporal_Mesh_C.h: el MAC vive
// en el slot 2 (no en el 0), no en el slot 0/1, para quedar a salvo de los 2
// ciclos "fantasma" que cgra_run reejecuta incondicionalmente al final de
// cada corrida (ST_WAIT_DONE/ST_DONE, ver comentario de cabecera de ese
// archivo para el detalle completo -- la misma leccion, ya pagada una vez,
// se reaplica aca sin volver a descubrirla).

#ifndef GEMM_2X2_TEMPORAL_MESH_C_H
#define GEMM_2X2_TEMPORAL_MESH_C_H

#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int GEMM_T_ROWS = 1;
static const int GEMM_T_COLS = 1;
static const int GEMM_T_DATA_W = 32;
static const int GEMM_T_VLEN = 1;
static const int GEMM_T_NUM_REGS = 8;
static const int GEMM_T_INSTR_MEM_SIZE = 3;  // slot0/1=NOP (a salvo del wrap fantasma), slot2=MAC real
static const int GEMM_T_NUM_PHASES = 2;       // k=0,1 -- un termino del producto punto por fase

typedef PE_MAC_State<GEMM_T_DATA_W, GEMM_T_VLEN, GEMM_T_NUM_REGS, GEMM_T_INSTR_MEM_SIZE>
    GemmTemporalCell_C;

typedef CGRA_Mesh_Static_C<GEMM_T_ROWS, GEMM_T_COLS, GEMM_T_DATA_W, GEMM_T_VLEN,
                            GemmTemporalCell_C>
    GemmTemporalMesh_C;
typedef GemmTemporalMesh_C::Link  GemmTemporalLink_C;
typedef GemmTemporalMesh_C::Instr GemmTemporalInstr_C;

// Programa temporal: slot0/slot1 = NOP, slot2 = acc += A[i][k]*B[k][j]
// (OP_MAC, src_a=NORTH, src_b=SOUTH), resultado corriente visible en out_E.
inline void gemm_temporal_program_c(
    GemmTemporalInstr_C prog[GEMM_T_ROWS][GEMM_T_COLS][GEMM_T_INSTR_MEM_SIZE])
{
    GemmTemporalInstr_C mac_ab;
    mac_ab.opcode = OP_MAC;
    mac_ab.src_a = SRC_NORTH;
    mac_ab.src_b = SRC_SOUTH;
    mac_ab.dst = DST_EAST;

    prog[0][0][0] = GemmTemporalInstr_C(); // NOP
    prog[0][0][1] = GemmTemporalInstr_C(); // NOP
    prog[0][0][2] = mac_ab;
}

#endif // GEMM_2X2_TEMPORAL_MESH_C_H
