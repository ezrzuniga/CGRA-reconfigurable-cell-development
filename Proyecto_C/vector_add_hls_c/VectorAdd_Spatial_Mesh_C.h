// VectorAdd_Spatial_Mesh_C.h
// Mapeo ESPACIAL de una suma de vectores de 4 elementos: 1 celda PE_Vector
// (VLEN=4, ROWS=1,COLS=1) -- los 4 elementos viajan como las 4 lanes de UN
// SOLO Link (PE_VectorData<32,4>) en cada puerto, asi que una unica
// instruccion ADD calcula las 4 sumas EN PARALELO, en 1 ciclo, en 1 sola
// invocacion. Contraparte de VectorAdd_Temporal_Mesh_C.h (PE_Scalar,
// VLEN=1, 4 invocaciones separadas).
//
// A diferencia de sum_reduction/GEMM/FIR (donde "espacial" significa mas
// CELDAS fisicas en la malla), aca "espacial" es paralelismo SIMD DENTRO de
// una sola celda -- ROWS=COLS=1 en AMBOS disenos de este par, la diferencia
// esta unicamente en VLEN (ancho del datapath), no en la cantidad de
// celdas. Es el eje de comparacion mas distinto de los 4 pares de este
// repositorio.

#ifndef VECTORADD_SPATIAL_MESH_C_H
#define VECTORADD_SPATIAL_MESH_C_H

#include "../pe_hls_c/vector/PE_Vector_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int VADD_S_ROWS = 1;
static const int VADD_S_COLS = 1;
static const int VADD_S_DATA_W = 32;
static const int VADD_S_VLEN = 4;            // SIMD -- las 4 lanes = los 4 elementos del vector
static const int VADD_S_NUM_REGS = 8;
static const int VADD_S_INSTR_MEM_SIZE = 1;
static const int VADD_S_NUM_PHASES = 1;

typedef PE_Vector_State<VADD_S_DATA_W, VADD_S_VLEN, VADD_S_NUM_REGS, VADD_S_INSTR_MEM_SIZE>
    VectorAddSpatialCell_C;

typedef CGRA_Mesh_Static_C<VADD_S_ROWS, VADD_S_COLS, VADD_S_DATA_W, VADD_S_VLEN,
                            VectorAddSpatialCell_C>
    VectorAddSpatialMesh_C;
typedef VectorAddSpatialMesh_C::Link  VectorAddSpatialLink_C;
typedef VectorAddSpatialMesh_C::Instr VectorAddSpatialInstr_C;

inline void vector_add_spatial_program_c(
    VectorAddSpatialInstr_C prog[VADD_S_ROWS][VADD_S_COLS][VADD_S_INSTR_MEM_SIZE])
{
    VectorAddSpatialInstr_C add_ab;
    add_ab.opcode = OP_ADD;
    add_ab.src_a = SRC_NORTH;
    add_ab.src_b = SRC_SOUTH;
    add_ab.dst = DST_EAST;
    prog[0][0][0] = add_ab;
}

#endif // VECTORADD_SPATIAL_MESH_C_H
