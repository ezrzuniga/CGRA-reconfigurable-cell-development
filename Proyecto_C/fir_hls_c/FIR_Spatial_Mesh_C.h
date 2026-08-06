// FIR_Spatial_Mesh_C.h
// Mapeo ESPACIAL de un filtro FIR de 3 taps: 4 celdas PE_MAC en una malla
// 1x4 (ROWS=1,COLS=4), una celda por muestra de salida y[n] (n=0..3), las 4
// calculadas EN PARALELO en una sola invocacion (NUM_PHASES=3, una fase por
// tap). Contraparte de FIR_Temporal_Mesh_C.h.
//
// Con ROWS=1, la fila 0 ES tambien la fila ROWS-1 -- por eso in_N/out_N e
// in_S/out_S son EXTERNOS para las 4 columnas a la vez (ver
// mesh_hls_c/CGRA_Mesh_Static_C.h::step_one). Eso es lo que hace este
// diseno mas simple que GEMM/sum-reduction espacial: cada celda tiene
// acceso externo completo (2 entradas + posibilidad de exportar por el
// mismo par de puertos) SIN necesitar relevos E/W entre celdas -- las 4
// celdas son completamente independientes entre si, no hay arbol ni
// combinacion cruzada.
//
// Fase k: w[k] entra por in_N[k][celda], x[n+k] entra por in_S[k][celda]
// (la ventana de entrada de cada celda ya viene desplazada por columna).
// La instruccion residente (slot 2, igual leccion de wrap fantasma que en
// FIR_Temporal_Mesh_C.h y sum_reduction_hls_c) hace
// acc += w[k]*x[n+k] (OP_MAC), exportando a out_N (legible por
// cgra_run como out_N[celda] al terminar, sin importar la columna).

#ifndef FIR_SPATIAL_MESH_C_H
#define FIR_SPATIAL_MESH_C_H

#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int FIR_S_ROWS = 1;
static const int FIR_S_COLS = 4;
static const int FIR_S_DATA_W = 32;
static const int FIR_S_VLEN = 1;
static const int FIR_S_NUM_REGS = 8;
static const int FIR_S_INSTR_MEM_SIZE = 3; // slot0/1=NOP, slot2=MAC real
static const int FIR_S_NUM_PHASES = 3;      // 3 taps

typedef PE_MAC_State<FIR_S_DATA_W, FIR_S_VLEN, FIR_S_NUM_REGS, FIR_S_INSTR_MEM_SIZE>
    FirSpatialCell_C;

typedef CGRA_Mesh_Static_C<FIR_S_ROWS, FIR_S_COLS, FIR_S_DATA_W, FIR_S_VLEN,
                            FirSpatialCell_C, FirSpatialCell_C,
                            FirSpatialCell_C, FirSpatialCell_C>
    FirSpatialMesh_C;
typedef FirSpatialMesh_C::Link  FirSpatialLink_C;
typedef FirSpatialMesh_C::Instr FirSpatialInstr_C;

inline void fir_spatial_program_c(
    FirSpatialInstr_C prog[FIR_S_ROWS][FIR_S_COLS][FIR_S_INSTR_MEM_SIZE])
{
    FirSpatialInstr_C mac_wx;
    mac_wx.opcode = OP_MAC;
    mac_wx.src_a = SRC_NORTH;
    mac_wx.src_b = SRC_SOUTH;
    mac_wx.dst = DST_NORTH; // externo para las 4 columnas, ver comentario de cabecera

    for (int c = 0; c < FIR_S_COLS; c++) {
        prog[0][c][0] = FirSpatialInstr_C(); // NOP
        prog[0][c][1] = FirSpatialInstr_C(); // NOP
        prog[0][c][2] = mac_wx;
    }
}

#endif // FIR_SPATIAL_MESH_C_H
