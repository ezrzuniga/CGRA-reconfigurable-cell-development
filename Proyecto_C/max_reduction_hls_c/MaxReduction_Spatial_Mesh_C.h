// MaxReduction_Spatial_Mesh_C.h
// Mapeo ESPACIAL de una reduccion por MAXIMO sobre 8 elementos: 4 celdas
// PE_Scalar en 2x2 (mismo layout fisico y misma asignacion de puertos
// externos que sum_reduction_hls_c/SumReduction_Spatial_Mesh_C.h), arbol de
// 3 niveles, 1 sola fase. Contraparte de MaxReduction_Temporal_Mesh_C.h.
//
// Igual que alli, max(a,b) no tiene opcode nativo -- cada combinacion
// necesita 4 instrucciones encadenadas (SUB, SLT, MUL, ADD), auto-
// referentes (leen el registro que estan actualizando), a diferencia del
// diseno de suma (SumReduction_Spatial_Mesh_C.h) donde cada ADD era un
// recalculo fresco sin auto-referencia y por eso podia fusionar computo+
// relevo en una sola instruccion. Aca NO se puede fusionar sin mas: el
// combine completo (4 instrucciones) debe terminar antes de poder relevar
// el resultado (5ta instruccion opcional) o antes de que el siguiente nivel
// pueda leerlo.
//
// Cronograma por celda (INSTR_MEM_SIZE=20, con margen generoso entre
// niveles en vez de calcular el margen minimo exacto -- misma filosofia
// de "corre de mas, no calcules al limite" que new_CGRA_guide.md
// recomienda para rutas con timing no obvio):
//
//   P00 (seed=N, combina con W, releva a E):
//     slot0-1 NOP | slot2 seed reg0=N | slot3-6 combine(reg0,W)->reg0 | slot7 releva E | slot8-19 NOP
//   P10 (seed=S, combina con W, releva a E): igual forma que P00.
//   P01 (seed=N, combina con E -> nivel1 sin relevo; margen; combina con W
//        (relevado por P00) -> nivel2, releva a S):
//     slot0-1 NOP | slot2 seed reg0=N | slot3-6 combine(reg0,E)->reg0 |
//     slot7-8 NOP (margen) | slot9-12 combine(reg0,W)->reg0 | slot13 releva S | slot14-19 NOP
//   P11 (seed=S, combina con E -> nivel1; margen; combina con W (relevado
//        por P10) -> nivel2 sin relevo; margen; combina con N (relevado por
//        P01) -> nivel3, releva a E -- resultado final):
//     slot0-1 NOP | slot2 seed reg0=S | slot3-6 combine(reg0,E)->reg0 |
//     slot7-8 NOP | slot9-12 combine(reg0,W)->reg0 | slot13-14 NOP |
//     slot15-18 combine(reg0,N)->reg0 | slot19 releva E (resultado final)
//
// Validado primero con un modelo Python del FSM exacto de cgra_run (mismo
// metodo que atrapo el bug de ciclos fantasma en el diseno temporal) antes
// de escribir este C++ -- las 4 instrucciones de cada combine son
// auto-referentes sobre reg0, pero como CADA combine completo vive dentro
// de un rango de slots >= 2 (nunca en slot0/1), los 2 ciclos fantasma de
// cgra_run nunca los interrumpen a mitad de camino.

#ifndef MAX_REDUCTION_SPATIAL_MESH_C_H
#define MAX_REDUCTION_SPATIAL_MESH_C_H

#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int MAXRED_S_ROWS = 2;
static const int MAXRED_S_COLS = 2;
static const int MAXRED_S_DATA_W = 32;
static const int MAXRED_S_VLEN = 1;
static const int MAXRED_S_NUM_REGS = 8;
static const int MAXRED_S_INSTR_MEM_SIZE = 20;
static const int MAXRED_S_NUM_PHASES = 1;

typedef PE_Scalar_State<MAXRED_S_DATA_W, MAXRED_S_VLEN, MAXRED_S_NUM_REGS, MAXRED_S_INSTR_MEM_SIZE>
    MaxRedSpatialCell_C;

typedef CGRA_Mesh_Static_C<MAXRED_S_ROWS, MAXRED_S_COLS, MAXRED_S_DATA_W, MAXRED_S_VLEN,
                            MaxRedSpatialCell_C, MaxRedSpatialCell_C,
                            MaxRedSpatialCell_C, MaxRedSpatialCell_C>
    MaxRedSpatialMesh_C;
typedef MaxRedSpatialMesh_C::Link  MaxRedSpatialLink_C;
typedef MaxRedSpatialMesh_C::Instr MaxRedSpatialInstr_C;

namespace maxred_spatial_detail {

inline MaxRedSpatialInstr_C seed(ap_uint<3> src) {
    MaxRedSpatialInstr_C i;
    i.opcode = OP_ADD; i.src_a = src; i.src_b = SRC_IMM; i.imm = 0;
    i.dst = DST_REG; i.reg_dst = 0;
    return i;
}

// 4 instrucciones: max(reg0, b_src) -> reg0. Escribe en out[0..3].
inline void combine4(MaxRedSpatialInstr_C out[4], ap_uint<3> b_src) {
    out[0].opcode = OP_SUB; out[0].src_a = b_src; out[0].src_b = SRC_REG; out[0].reg_b = 0;
    out[0].dst = DST_REG; out[0].reg_dst = 1;                       // diff = b - reg0

    out[1].opcode = OP_SLT; out[1].src_a = SRC_REG; out[1].reg_a = 0; out[1].src_b = b_src;
    out[1].dst = DST_REG; out[1].reg_dst = 2;                       // flag = reg0 < b

    out[2].opcode = OP_MUL; out[2].src_a = SRC_REG; out[2].reg_a = 1; out[2].src_b = SRC_REG; out[2].reg_b = 2;
    out[2].dst = DST_REG; out[2].reg_dst = 3;                       // term = diff*flag

    out[3].opcode = OP_ADD; out[3].src_a = SRC_REG; out[3].reg_a = 0; out[3].src_b = SRC_REG; out[3].reg_b = 3;
    out[3].dst = DST_REG; out[3].reg_dst = 0;                       // reg0 = reg0+term
}

inline MaxRedSpatialInstr_C relay(ap_uint<3> dst) {
    MaxRedSpatialInstr_C i;
    i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = 0; i.dst = dst;
    return i;
}

} // namespace maxred_spatial_detail

inline void maxred_spatial_program_c(
    MaxRedSpatialInstr_C prog[MAXRED_S_ROWS][MAXRED_S_COLS][MAXRED_S_INSTR_MEM_SIZE])
{
    using namespace maxred_spatial_detail;
    MaxRedSpatialInstr_C nop; // OP_NOP por defecto

    // P00 (0,0): seed=N, combine con W, releva E.
    {
        auto& p = prog[0][0];
        p[0] = nop; p[1] = nop;
        p[2] = seed(SRC_NORTH);
        MaxRedSpatialInstr_C c[4]; combine4(c, SRC_WEST);
        p[3] = c[0]; p[4] = c[1]; p[5] = c[2]; p[6] = c[3];
        p[7] = relay(DST_EAST);
        for (int s = 8; s < MAXRED_S_INSTR_MEM_SIZE; s++) p[s] = nop;
    }
    // P10 (1,0): seed=S, combine con W, releva E.
    {
        auto& p = prog[1][0];
        p[0] = nop; p[1] = nop;
        p[2] = seed(SRC_SOUTH);
        MaxRedSpatialInstr_C c[4]; combine4(c, SRC_WEST);
        p[3] = c[0]; p[4] = c[1]; p[5] = c[2]; p[6] = c[3];
        p[7] = relay(DST_EAST);
        for (int s = 8; s < MAXRED_S_INSTR_MEM_SIZE; s++) p[s] = nop;
    }
    // P01 (0,1): seed=N, combine con E (nivel1, sin relevo), margen,
    //            combine con W (nivel2, relevado por P00), releva S.
    {
        auto& p = prog[0][1];
        p[0] = nop; p[1] = nop;
        p[2] = seed(SRC_NORTH);
        MaxRedSpatialInstr_C c1[4]; combine4(c1, SRC_EAST);
        p[3] = c1[0]; p[4] = c1[1]; p[5] = c1[2]; p[6] = c1[3];
        p[7] = nop; p[8] = nop; // margen
        MaxRedSpatialInstr_C c2[4]; combine4(c2, SRC_WEST);
        p[9] = c2[0]; p[10] = c2[1]; p[11] = c2[2]; p[12] = c2[3];
        p[13] = relay(DST_SOUTH);
        for (int s = 14; s < MAXRED_S_INSTR_MEM_SIZE; s++) p[s] = nop;
    }
    // P11 (1,1): seed=S, combine con E (nivel1), margen, combine con W
    //            (nivel2, relevado por P10, sin relevo propio), margen,
    //            combine con N (nivel3, relevado por P01), releva E (final).
    {
        auto& p = prog[1][1];
        p[0] = nop; p[1] = nop;
        p[2] = seed(SRC_SOUTH);
        MaxRedSpatialInstr_C c1[4]; combine4(c1, SRC_EAST);
        p[3] = c1[0]; p[4] = c1[1]; p[5] = c1[2]; p[6] = c1[3];
        p[7] = nop; p[8] = nop; // margen
        MaxRedSpatialInstr_C c2[4]; combine4(c2, SRC_WEST);
        p[9] = c2[0]; p[10] = c2[1]; p[11] = c2[2]; p[12] = c2[3];
        p[13] = nop; p[14] = nop; // margen
        MaxRedSpatialInstr_C c3[4]; combine4(c3, SRC_NORTH);
        p[15] = c3[0]; p[16] = c3[1]; p[17] = c3[2]; p[18] = c3[3];
        p[19] = relay(DST_EAST); // resultado final
    }
}

#endif // MAX_REDUCTION_SPATIAL_MESH_C_H
