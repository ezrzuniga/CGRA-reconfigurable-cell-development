// SumReduction_Temporal_Mesh_C.h
// Mapeo TEMPORAL de la reduccion por suma sobre el template generico
// cgra_run<...> (ver ../cgra_hls_c/CGRA_Top_C.h): una unica PE_MAC
// (ROWS=1,COLS=1), un unico slot de instr_mem que se reejecuta una vez por
// fase (NUM_PHASES=8, uno por elemento del vector). No hay paralelismo
// espacial -- todo el trabajo cae en la misma celda fisica, repartido en el
// tiempo. Contraparte de SumReduction_Spatial_Mesh_C.h (4 PE_Scalar en 2x2,
// arbol de sumas en paralelo) para comparar cycles/recursos de los dos
// estilos de mapeo sobre el MISMO problema.
//
// Instruccion residente (slot 2 de 3, ver mas abajo por que no es slot 0):
// acc += in_N * 1 (OP_MAC con src_b=SRC_IMM=1 hace que "acc += a*b" se
// reduzca a "acc += a"), dst=EAST para que el acumulador quede visible en
// out_E -- mismo patron que
// ../../Proyecto_SystemC/pe/mac/PE_MAC_SumReduction__TB.cpp, pero orquestado
// por cgra_run en vez de un testbench SystemC manual: cada FASE presenta un
// elemento nuevo en in_N[fase][0].
//
// mesh_clear_acc() (parte de cgra_run, antes de la fase 0) deja acc=0 al
// arrancar cada corrida -- no hace falta instruccion de "seed" aparte; el
// primer MAC ya sale de acc=0.
//
// Por que INSTR_MEM_SIZE=3 (con 2 slots de NOP) y NO 1 (leccion no obvia,
// pagada verificando esto con un modelo Python del FSM antes de confiar en
// el diseno): el for(;;) de cgra_run (CGRA_Top_C.h) llama mesh_step() de
// forma INCONDICIONAL en cada iteracion, incluidas las 2 iteraciones de los
// estados ST_WAIT_DONE/ST_DONE al final de la corrida -- ahi el pc de cada
// celda, que ya volvio a 0 (wrap), se re-ejecuta 2 veces mas ("ciclos
// fantasma") usando los bordes de la ULTIMA fase, todavia sostenidos (sin
// cambiar). Con INSTR_MEM_SIZE=1 el unico slot ES la instruccion MAC, asi
// que esos 2 ciclos fantasma vuelven a hacer acc+=in_N con el MISMO ultimo
// elemento -- lo suma 2 veces de mas (bug real, confirmado con el modelo:
// total quedaba en 28 en vez de 22 para V={6,-2,9,4,0,7,-5,3}). GEMM evita
// esto porque su instruccion critica (MOV ACC->puerto) vive en el ULTIMO
// slot (3 de 4), nunca en los slots 0/1 que son los que el wrap-around
// fantasma siempre re-ejecuta -- aca se aplica el mismo principio: los slots
// 0 y 1 son NOP (inofensivos si se repiten), y el MAC (la unica instruccion
// no-idempotente, porque acc+=r depende de su propio valor previo) vive en
// el slot 2, fuera del alcance de los 2 ciclos fantasma.

#ifndef SUM_REDUCTION_TEMPORAL_MESH_C_H
#define SUM_REDUCTION_TEMPORAL_MESH_C_H

#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int SUMRED_T_ROWS = 1;
static const int SUMRED_T_COLS = 1;
static const int SUMRED_T_DATA_W = 32;
static const int SUMRED_T_VLEN = 1;
static const int SUMRED_T_NUM_REGS = 8;
static const int SUMRED_T_INSTR_MEM_SIZE = 3;   // slot0/1=NOP (a salvo del wrap fantasma), slot2=MAC real
static const int SUMRED_T_NUM_PHASES = 8;        // 1 fase por elemento del vector (temporal)

typedef PE_MAC_State<SUMRED_T_DATA_W, SUMRED_T_VLEN, SUMRED_T_NUM_REGS, SUMRED_T_INSTR_MEM_SIZE>
    SumRedTemporalCell_C;

typedef CGRA_Mesh_Static_C<SUMRED_T_ROWS, SUMRED_T_COLS, SUMRED_T_DATA_W, SUMRED_T_VLEN,
                            SumRedTemporalCell_C>
    SumRedTemporalMesh_C;
typedef SumRedTemporalMesh_C::Link  SumRedTemporalLink_C;
typedef SumRedTemporalMesh_C::Instr SumRedTemporalInstr_C;

// Programa temporal: slot0/slot1 = NOP (relleno, a salvo del wrap fantasma
// de cgra_run), slot2 = acc += in_N (via MAC con b=IMM=1), resultado
// corriente visible en out_E.
inline void sumred_temporal_program_c(
    SumRedTemporalInstr_C prog[SUMRED_T_ROWS][SUMRED_T_COLS][SUMRED_T_INSTR_MEM_SIZE])
{
    SumRedTemporalInstr_C mac_acc;
    mac_acc.opcode = OP_MAC;
    mac_acc.src_a = SRC_NORTH;
    mac_acc.src_b = SRC_IMM;
    mac_acc.imm = 1;
    mac_acc.dst = DST_EAST;

    prog[0][0][0] = SumRedTemporalInstr_C(); // NOP
    prog[0][0][1] = SumRedTemporalInstr_C(); // NOP
    prog[0][0][2] = mac_acc;
}

#endif // SUM_REDUCTION_TEMPORAL_MESH_C_H
