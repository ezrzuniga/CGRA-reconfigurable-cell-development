// VectorAdd_Temporal_Mesh_C.h
// Mapeo TEMPORAL de una suma de vectores de 4 elementos: 1 celda PE_Scalar
// (VLEN=1, ROWS=1,COLS=1), invocada 4 veces (una por elemento c[n] =
// a[n]+b[n], n=0..3) -- sin SIMD, un elemento por invocacion. Contraparte de
// VectorAdd_Spatial_Mesh_C.h (1 celda PE_Vector, VLEN=4, calcula los 4
// elementos EN UN SOLO ciclo/instruccion via lanes SIMD).
//
// A diferencia de sum_reduction/GEMM/FIR (que usan PE_MAC porque acumulan),
// esta suma NO acumula -- cada c[n] es independiente, asi que ADD (no MAC)
// alcanza, y como ADD no es auto-referente (no lee su propio resultado
// previo), el ciclo de wrap fantasma de cgra_run es inofensivo aca: repetir
// ADD con los mismos operandos sostenidos solo recalcula el mismo valor, no
// hay bug de doble conteo que evitar. Por eso el programa cabe en un unico
// slot (INSTR_MEM_SIZE=1) sin necesitar relleno NOP defensivo -- distinto
// de SumReduction_Temporal_Mesh_C.h/FIR_Temporal_Mesh_C.h.

#ifndef VECTORADD_TEMPORAL_MESH_C_H
#define VECTORADD_TEMPORAL_MESH_C_H

#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int VADD_T_ROWS = 1;
static const int VADD_T_COLS = 1;
static const int VADD_T_DATA_W = 32;
static const int VADD_T_VLEN = 1;           // sin SIMD -- 1 elemento por invocacion
static const int VADD_T_NUM_REGS = 8;
static const int VADD_T_INSTR_MEM_SIZE = 1; // ADD no es auto-referente: seguro en slot0
static const int VADD_T_NUM_PHASES = 1;
static const int VADD_N = 4;                 // longitud del vector

typedef PE_Scalar_State<VADD_T_DATA_W, VADD_T_VLEN, VADD_T_NUM_REGS, VADD_T_INSTR_MEM_SIZE>
    VectorAddTemporalCell_C;

typedef CGRA_Mesh_Static_C<VADD_T_ROWS, VADD_T_COLS, VADD_T_DATA_W, VADD_T_VLEN,
                            VectorAddTemporalCell_C>
    VectorAddTemporalMesh_C;
typedef VectorAddTemporalMesh_C::Link  VectorAddTemporalLink_C;
typedef VectorAddTemporalMesh_C::Instr VectorAddTemporalInstr_C;

inline void vector_add_temporal_program_c(
    VectorAddTemporalInstr_C prog[VADD_T_ROWS][VADD_T_COLS][VADD_T_INSTR_MEM_SIZE])
{
    VectorAddTemporalInstr_C add_ab;
    add_ab.opcode = OP_ADD;
    add_ab.src_a = SRC_NORTH;
    add_ab.src_b = SRC_SOUTH;
    add_ab.dst = DST_EAST;
    prog[0][0][0] = add_ab;
}

#endif // VECTORADD_TEMPORAL_MESH_C_H
