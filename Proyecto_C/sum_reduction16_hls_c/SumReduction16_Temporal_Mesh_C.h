// SumReduction16_Temporal_Mesh_C.h
// Mapeo TEMPORAL de una reduccion por suma de 16 elementos (contraparte de
// n=8 en sum_reduction_hls_c/SumReduction_Temporal_Mesh_C.h): identico
// diseno de 1 celda PE_MAC, solo con NUM_PHASES=16 en vez de 8 -- escalar
// el mapeo temporal a un vector mas largo es un cambio de 1 constante,
// nada mas cambia (mismo slot2=MAC, mismos slots0/1=NOP de proteccion).
// Sirve para responder la pregunta abierta que dejo
// sum_reduction_hls_c/README.md: ¿el mapeo espacial le gana al temporal en
// ciclos reales una vez que N crece?  (Ver SumReduction16_Spatial_Mesh_C.h
// para la contraparte espacial, que si necesita un diseno nuevo porque una
// malla 2x2 solo tiene 8 puertos externos simultaneos.)

#ifndef SUM_REDUCTION16_TEMPORAL_MESH_C_H
#define SUM_REDUCTION16_TEMPORAL_MESH_C_H

#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int SUMRED16_T_ROWS = 1;
static const int SUMRED16_T_COLS = 1;
static const int SUMRED16_T_DATA_W = 32;
static const int SUMRED16_T_VLEN = 1;
static const int SUMRED16_T_NUM_REGS = 8;
static const int SUMRED16_T_INSTR_MEM_SIZE = 3; // slot0/1=NOP, slot2=MAC real
static const int SUMRED16_T_NUM_PHASES = 16;      // 1 fase por elemento -- n=16

typedef PE_MAC_State<SUMRED16_T_DATA_W, SUMRED16_T_VLEN, SUMRED16_T_NUM_REGS, SUMRED16_T_INSTR_MEM_SIZE>
    SumRed16TemporalCell_C;

typedef CGRA_Mesh_Static_C<SUMRED16_T_ROWS, SUMRED16_T_COLS, SUMRED16_T_DATA_W, SUMRED16_T_VLEN,
                            SumRed16TemporalCell_C>
    SumRed16TemporalMesh_C;
typedef SumRed16TemporalMesh_C::Link  SumRed16TemporalLink_C;
typedef SumRed16TemporalMesh_C::Instr SumRed16TemporalInstr_C;

inline void sumred16_temporal_program_c(
    SumRed16TemporalInstr_C prog[SUMRED16_T_ROWS][SUMRED16_T_COLS][SUMRED16_T_INSTR_MEM_SIZE])
{
    SumRed16TemporalInstr_C mac_acc;
    mac_acc.opcode = OP_MAC;
    mac_acc.src_a = SRC_NORTH;
    mac_acc.src_b = SRC_IMM;
    mac_acc.imm = 1;
    mac_acc.dst = DST_EAST;

    prog[0][0][0] = SumRed16TemporalInstr_C(); // NOP
    prog[0][0][1] = SumRed16TemporalInstr_C(); // NOP
    prog[0][0][2] = mac_acc;
}

#endif // SUM_REDUCTION16_TEMPORAL_MESH_C_H
