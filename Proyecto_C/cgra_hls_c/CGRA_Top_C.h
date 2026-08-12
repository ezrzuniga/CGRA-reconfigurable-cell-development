// CGRA_Top_C.h
// Template generico de "top de Vitis HLS para una CGRA reprogramable",
// parametrizado por ROWS/COLS/DATA_W/VLEN/NUM_REGS/INSTR_MEM_SIZE/NUM_PHASES.
// De aca "se saca" una CGRA sintetizable especifica: la aplicacion concreta
// (ver gemm_hls_c/GEMM_2x2_HLS_Top_C.cpp) solo necesita declarar su propio
// `static Mesh mesh;` (el unico estado con memoria del diseno -- persiste
// entre invocaciones del top, ver mas abajo por que) y reenviar sus puertos
// a cgra_run<...>() con sus propios parametros de plantilla. cgra_run() en
// si no tiene estado propio (el mesh se pasa por referencia) para quedar
// componible/reutilizable.
//
// Dos caminos de control, mutuamente excluyentes por llamada (contrato de
// uso del host, no impuesto en hardware -- no pulsar prog_valid y start en
// la misma llamada, ni programar mientras una corrida esta en curso):
//
// 1) prog_valid=true: escribe una unica instruccion (prog_row,prog_col,
//    prog_slot,prog_instr) en la memoria de instrucciones de esa PE via
//    mesh_program() (CGRA_Mesh_Static_C.h) -- una transaccion de "poke",
//    independiente de cualquier corrida, done=true de inmediato. Como el
//    mesh del wrapper de aplicacion es `static`, el programa sube una vez y
//    sobrevive a corridas posteriores con operandos distintos -- CGRA
//    reconfigurable de verdad, no un programa fijo en el hardware.
//
// 2) start=true: corre la secuencia completa de NUM_PHASES fases en una
//    sola invocacion (igual que el GEMM_2x2_HLS_Top_C original), leyendo el
//    programa YA RESIDENTE en instr_mem (subido antes via prog_valid). Por
//    fase se presenta un nuevo snapshot de los 4 bordes de la malla
//    (in_N/in_S/in_W/in_E, dimensionados por ROWS/COLS con un eje extra de
//    fase) y se corren INSTR_MEM_SIZE ciclos de mesh_step -- una fase =
//    una pasada completa por la memoria de instrucciones de cada PE.
//
// Por que ya no hace falta un estado RELOAD (el original si lo tenia): ese
// estado existia unicamente para volver a cargar el programa real en
// instr_mem cada corrida (porque antes instr_mem NO era persistente). Ahora
// que instr_mem sobrevive entre llamadas, RELOAD desaparece por completo --
// la unica preparacion que sigue haciendo falta antes de la fase 0 es
// limpiar los acumuladores (mesh_clear_acc(), canal directo, ya no hace
// falta "pedir prestada" instr_mem para eso) y alinear los pc de las PEs a 0
// (pulso de rst, igual que antes).
//
// Disciplina de registro preservada del diseno original ("Nota de
// temporizacion" en gemm_hls/GEMM_2x2_HLS_Top.h): todo lo que se le
// "presenta" a la malla en un ciclo solo es efectivo para la malla el ciclo
// SIGUIENTE -- se modela con el registro explicito MeshDrive (`curr`/`nxt`,
// "pegajoso": retiene el valor anterior salvo que el estado actual lo
// sobreescriba). Por eso cada fase se "prepara" un ciclo antes de que su
// propio estado la consuma -- mismo patron ya validado con csim/cosim
// reales para GEMM 2x2, solo generalizado a NUM_PHASES arbitrario.

#ifndef CGRA_TOP_C_H
#define CGRA_TOP_C_H

#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

namespace cgra_hls_c_detail {

template <int DATA_W, int VLEN, int COLS, int ROWS>
struct MeshDrive {
    bool rst;
    // Los 4 bordes de la malla se presentan enteros en el mismo ciclo (cada
    // celda de borde lee el suyo): registros, no RAM -- si no, presentar una
    // fase costaria COLS+ROWS ciclos de lectura en vez de uno.
    PE_VectorData<DATA_W, VLEN> bound_in_N[COLS];
    PE_VectorData<DATA_W, VLEN> bound_in_S[COLS];
    PE_VectorData<DATA_W, VLEN> bound_in_W[ROWS];
    PE_VectorData<DATA_W, VLEN> bound_in_E[ROWS];
#pragma HLS ARRAY_PARTITION variable=bound_in_N complete dim=1
#pragma HLS ARRAY_PARTITION variable=bound_in_S complete dim=1
#pragma HLS ARRAY_PARTITION variable=bound_in_W complete dim=1
#pragma HLS ARRAY_PARTITION variable=bound_in_E complete dim=1

    MeshDrive() : rst(false) {}
};

enum CgraRunState { ST_REALIGN, ST_PHASE_RUN, ST_WAIT_DONE, ST_DONE };

template <int DATA_W, int VLEN, int COLS, int ROWS, int NUM_PHASES>
inline void present_phase(
    MeshDrive<DATA_W, VLEN, COLS, ROWS>& d, int phase,
    const PE_VectorData<DATA_W, VLEN> in_N[NUM_PHASES][COLS],
    const PE_VectorData<DATA_W, VLEN> in_S[NUM_PHASES][COLS],
    const PE_VectorData<DATA_W, VLEN> in_W[NUM_PHASES][ROWS],
    const PE_VectorData<DATA_W, VLEN> in_E[NUM_PHASES][ROWS])
{
#pragma HLS INLINE
present_cols_loop:
    for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
        d.bound_in_N[c] = in_N[phase][c];
        d.bound_in_S[c] = in_S[phase][c];
    }
present_rows_loop:
    for (int r = 0; r < ROWS; r++) {
#pragma HLS UNROLL
        d.bound_in_W[r] = in_W[phase][r];
        d.bound_in_E[r] = in_E[phase][r];
    }
}

} // namespace cgra_hls_c_detail

// INSTR_MEM_SIZE queda como parametro explicito de cgra_run (no se puede
// deducir de CellTs...): es "cuantos ciclos dura una fase" para TODAS las
// celdas de la malla por igual -- una suposicion simplificadora razonable
// (incluso para celdas sin instr_mem propio, como routing/memoria, que no
// tienen un concepto de "una pasada" en el mismo sentido, pero igual
// necesitan que la FSM generica sepa un unico numero de ciclos por fase).
template <int ROWS, int COLS, int DATA_W, int VLEN, int INSTR_MEM_SIZE, int NUM_PHASES, typename... CellTs>
void cgra_run(
    CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    const PE_Instruction<DATA_W>& prog_instr,
    bool start, bool& done,
    const PE_VectorData<DATA_W, VLEN> in_N[NUM_PHASES][COLS],
    const PE_VectorData<DATA_W, VLEN> in_S[NUM_PHASES][COLS],
    const PE_VectorData<DATA_W, VLEN> in_W[NUM_PHASES][ROWS],
    const PE_VectorData<DATA_W, VLEN> in_E[NUM_PHASES][ROWS],
    PE_VectorData<DATA_W, VLEN> out_N[COLS], PE_VectorData<DATA_W, VLEN> out_S[COLS],
    PE_VectorData<DATA_W, VLEN> out_W[ROWS], PE_VectorData<DATA_W, VLEN> out_E[ROWS])
{
    using namespace cgra_hls_c_detail;
    typedef MeshDrive<DATA_W, VLEN, COLS, ROWS> Drive;

    done = false;

    if (prog_valid) {
        mesh_program(mesh, prog_row, prog_col, prog_slot, prog_instr);
        done = true;
        return;
    }

    if (!start) return;

    mesh_clear_acc(mesh);

    // rst=true desde la primera llamada a mesh_step (no un ciclo despues,
    // como en el GEMM_2x2_HLS_Top_C original): la malla ahora es persistente
    // entre invocaciones, asi que el pc y el resto de senales internas
    // quedan en lo que haya dejado la corrida anterior. Un primer ciclo
    // "fantasma" con rst=false ejecutaria una instruccion real de ese pc
    // viejo (con la instr_mem YA cargada) usando bordes en cero -- si esa
    // instruccion es un MAC, contaminaria el acumulador justo despues de
    // limpiarlo con mesh_clear_acc(). Aplicar el reset de una vez evita esa
    // ejecucion fantasma por completo.
    Drive curr;
    curr.rst = true;
    CgraRunState state = ST_REALIGN;
    int phase = 0;
    ap_uint<16> cnt = 0;

    // cgra_cycle_loop NO lleva PIPELINE: es la FSM de control, y cada iteracion
    // depende del estado que dejo la anterior (pc de las PEs, acumuladores,
    // salidas de la malla via el snapshot). Un ciclo de malla por iteracion ya
    // es el objetivo -- el paralelismo esta ADENTRO de mesh_step (las ROWS*COLS
    // PEs en paralelo, ver la nota de pragmas en CGRA_Mesh_Static_C.h), no
    // entre iteraciones de este bucle.
cgra_cycle_loop:
    for (;;) {
        mesh_step(mesh, curr.rst, /*enable=*/true,
                  curr.bound_in_N, curr.bound_in_S, curr.bound_in_W, curr.bound_in_E);

        Drive nxt = curr; // pegajoso: retiene lo del ciclo anterior salvo que este case lo cambie

        switch (state) {
            case ST_REALIGN:
                nxt.rst = false; // el pulso de reset dura exactamente este primer ciclo
                present_phase<DATA_W, VLEN, COLS, ROWS, NUM_PHASES>(nxt, 0, in_N, in_S, in_W, in_E);
                state = ST_PHASE_RUN;
                phase = 0;
                cnt = 0;
                break;

            case ST_PHASE_RUN:
                nxt.rst = false;
                if (cnt == INSTR_MEM_SIZE - 1) {
                    if (phase == NUM_PHASES - 1) {
                        state = ST_WAIT_DONE;
                    } else {
                        phase = phase + 1;
                        present_phase<DATA_W, VLEN, COLS, ROWS, NUM_PHASES>(nxt, phase, in_N, in_S, in_W, in_E);
                        cnt = 0;
                    }
                } else {
                    cnt = cnt + 1;
                }
                break;

            case ST_WAIT_DONE:
                state = ST_DONE;
                break;

            case ST_DONE: {
                // mesh.pe es un std::tuple heterogeneo -- no se puede indexar
                // [r][c] en runtime. mesh_read_outputs() vuelca las salidas de
                // las ROWS*COLS celdas (desenrollado en tiempo de compilacion,
                // ver CGRA_Mesh_Static_C.h) en arreglos Link[ROWS][COLS]
                // homogeneos, que si se pueden indexar en runtime como
                // cualquier arreglo comun.
                typedef PE_VectorData<DATA_W, VLEN> Link;
                Link all_out_N[ROWS][COLS], all_out_S[ROWS][COLS];
                Link all_out_E[ROWS][COLS], all_out_W[ROWS][COLS];
#pragma HLS ARRAY_PARTITION variable=all_out_N complete dim=0
#pragma HLS ARRAY_PARTITION variable=all_out_S complete dim=0
#pragma HLS ARRAY_PARTITION variable=all_out_E complete dim=0
#pragma HLS ARRAY_PARTITION variable=all_out_W complete dim=0
                mesh_read_outputs(mesh, all_out_N, all_out_S, all_out_E, all_out_W);

                for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
                    out_N[c] = all_out_N[0][c];
                    out_S[c] = all_out_S[ROWS - 1][c];
                }
                for (int r = 0; r < ROWS; r++) {
#pragma HLS UNROLL
                    out_W[r] = all_out_W[r][0];
                    out_E[r] = all_out_E[r][COLS - 1];
                }
                done = true;
                return;
            }
        }

        curr = nxt;
    }
}

#endif // CGRA_TOP_C_H
