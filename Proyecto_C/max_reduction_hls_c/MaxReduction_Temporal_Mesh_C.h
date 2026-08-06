// MaxReduction_Temporal_Mesh_C.h
// Mapeo TEMPORAL de una reduccion por MAXIMO sobre 8 elementos: 1 celda
// PE_Scalar (ROWS=1,COLS=1), reutilizada 8 fases (una por elemento).
// Contraparte de MaxReduction_Spatial_Mesh_C.h (arbol de 4 celdas en 2x2).
//
// A diferencia de sum_reduction (que tiene MAC nativo, acc+=a*b en una sola
// instruccion), esta ISA NO tiene una opcode MAX ni un select/mux
// condicional -- hay que construirlo con aritmetica:
//   max(a,b) = a + (b-a)*[a<b]     donde [a<b] = SLT(a,b) es 0 o 1
// Eso son 4 instrucciones encadenadas (SUB, SLT, MUL, ADD) por cada
// combinacion, en vez de la 1 instruccion que bastaba para sum reduction --
// el costo real de no tener una ALU con select nativo.
//
// slot0/1 = NOP (proteccion contra el wrap fantasma de cgra_run, igual
// leccion que SumReduction_Temporal_Mesh_C.h): con reg0 = maximo corriente,
// las 4 instrucciones de combinacion (slots 2-5) actualizan reg0
// LEYENDO SU PROPIO VALOR ANTERIOR -- son auto-referentes, igual que el
// MAC de sum reduction, asi que deben quedar fuera del alcance de los 2
// ciclos fantasma (que siempre re-ejecutan exactamente slot0 y slot1).
// slot6 exporta reg0 a out_E cada fase (el ultimo valor exportado, tras la
// fase 7, es el que cgra_run lee al terminar).
//
// reg0 arranca en 0 (la malla se inicializa a cero, ver GEMM_2x2_Mesh_C.h
// para el mismo precedente) -- por eso los vectores de prueba usan un
// maximo real >= 0 (mismos 2 vectores de referencia que sum_reduction_hls_c,
// que ya cumplen esto), para que max(0, v0..v7) == max(v0..v7) sin
// necesitar una instruccion de "seed" aparte.

#ifndef MAX_REDUCTION_TEMPORAL_MESH_C_H
#define MAX_REDUCTION_TEMPORAL_MESH_C_H

#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int MAXRED_T_ROWS = 1;
static const int MAXRED_T_COLS = 1;
static const int MAXRED_T_DATA_W = 32;
static const int MAXRED_T_VLEN = 1;
static const int MAXRED_T_NUM_REGS = 8;
static const int MAXRED_T_INSTR_MEM_SIZE = 7; // 2 NOP + 4 combine + 1 export
static const int MAXRED_T_NUM_PHASES = 8;      // 1 fase por elemento

typedef PE_Scalar_State<MAXRED_T_DATA_W, MAXRED_T_VLEN, MAXRED_T_NUM_REGS, MAXRED_T_INSTR_MEM_SIZE>
    MaxRedTemporalCell_C;

typedef CGRA_Mesh_Static_C<MAXRED_T_ROWS, MAXRED_T_COLS, MAXRED_T_DATA_W, MAXRED_T_VLEN,
                            MaxRedTemporalCell_C>
    MaxRedTemporalMesh_C;
typedef MaxRedTemporalMesh_C::Link  MaxRedTemporalLink_C;
typedef MaxRedTemporalMesh_C::Instr MaxRedTemporalInstr_C;

inline void maxred_temporal_program_c(
    MaxRedTemporalInstr_C prog[MAXRED_T_ROWS][MAXRED_T_COLS][MAXRED_T_INSTR_MEM_SIZE])
{
    MaxRedTemporalInstr_C diff, flag, term, upd, exp;

    diff.opcode = OP_SUB; diff.src_a = SRC_NORTH; diff.src_b = SRC_REG; diff.reg_b = 0;
    diff.dst = DST_REG; diff.reg_dst = 1;                          // diff = N - reg0

    flag.opcode = OP_SLT; flag.src_a = SRC_REG; flag.reg_a = 0; flag.src_b = SRC_NORTH;
    flag.dst = DST_REG; flag.reg_dst = 2;                          // flag = reg0 < N

    term.opcode = OP_MUL; term.src_a = SRC_REG; term.reg_a = 1; term.src_b = SRC_REG; term.reg_b = 2;
    term.dst = DST_REG; term.reg_dst = 3;                          // term = diff*flag

    upd.opcode = OP_ADD; upd.src_a = SRC_REG; upd.reg_a = 0; upd.src_b = SRC_REG; upd.reg_b = 3;
    upd.dst = DST_REG; upd.reg_dst = 0;                            // reg0 = reg0+term

    exp.opcode = OP_MOV; exp.src_a = SRC_REG; exp.reg_a = 0; exp.dst = DST_EAST; // export

    prog[0][0][0] = MaxRedTemporalInstr_C(); // NOP
    prog[0][0][1] = MaxRedTemporalInstr_C(); // NOP
    prog[0][0][2] = diff;
    prog[0][0][3] = flag;
    prog[0][0][4] = term;
    prog[0][0][5] = upd;
    prog[0][0][6] = exp;
}

#endif // MAX_REDUCTION_TEMPORAL_MESH_C_H
