// FIR_Temporal_Mesh_C.h
// Mapeo TEMPORAL de un filtro FIR de 3 taps sobre cgra_run<...>: una unica
// celda PE_MAC (ROWS=1,COLS=1) reutilizada 4 veces (una invocacion
// start=true por muestra de salida y[n], n=0..3), cada corrida acumula
// y[n] = w0*x[n] + w1*x[n+1] + w2*x[n+2] en 3 fases (una por tap k=0,1,2).
// Contraparte de FIR_Spatial_Mesh_C.h (4 celdas PE_MAC en 1x4, las 4
// muestras se calculan en paralelo). Mismo esqueleto que
// gemm_temporal_hls_c/GEMM_2x2_Temporal_Mesh_C.h -- literalmente el mismo
// patron (acc += a*b por fase, MAC en el slot 2 para evitar el bug de
// ciclos fantasma), solo con 3 fases en vez de 2 y semantica de "filtro" en
// vez de "producto de matrices".
//
// Cada fase k presenta el peso w[k] por in_N y la muestra de entrada
// x[n+k] por in_S; la instruccion residente hace acc += w[k]*x[n+k]
// (OP_MAC), exportando a out_E. mesh_clear_acc() (parte de cgra_run, al
// inicio de cada start=true) deja acc=0 en cada corrida.

#ifndef FIR_TEMPORAL_MESH_C_H
#define FIR_TEMPORAL_MESH_C_H

#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int FIR_T_ROWS = 1;
static const int FIR_T_COLS = 1;
static const int FIR_T_DATA_W = 32;
static const int FIR_T_VLEN = 1;
static const int FIR_T_NUM_REGS = 8;
static const int FIR_T_INSTR_MEM_SIZE = 3; // slot0/1=NOP (wrap fantasma), slot2=MAC real
static const int FIR_T_NUM_PHASES = 3;      // 3 taps (k=0,1,2)
static const int FIR_TAPS = 3;

typedef PE_MAC_State<FIR_T_DATA_W, FIR_T_VLEN, FIR_T_NUM_REGS, FIR_T_INSTR_MEM_SIZE>
    FirTemporalCell_C;

typedef CGRA_Mesh_Static_C<FIR_T_ROWS, FIR_T_COLS, FIR_T_DATA_W, FIR_T_VLEN,
                            FirTemporalCell_C>
    FirTemporalMesh_C;
typedef FirTemporalMesh_C::Link  FirTemporalLink_C;
typedef FirTemporalMesh_C::Instr FirTemporalInstr_C;

inline void fir_temporal_program_c(
    FirTemporalInstr_C prog[FIR_T_ROWS][FIR_T_COLS][FIR_T_INSTR_MEM_SIZE])
{
    FirTemporalInstr_C mac_wx;
    mac_wx.opcode = OP_MAC;
    mac_wx.src_a = SRC_NORTH;
    mac_wx.src_b = SRC_SOUTH;
    mac_wx.dst = DST_EAST;

    prog[0][0][0] = FirTemporalInstr_C(); // NOP
    prog[0][0][1] = FirTemporalInstr_C(); // NOP
    prog[0][0][2] = mac_wx;
}

#endif // FIR_TEMPORAL_MESH_C_H
