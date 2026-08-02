// CGRA_Mesh_Static_C.h
// Transliteracion a C/C++ puro de mesh_hls/CGRA_Mesh_Static.h: mismo wiring
// estatico ROWS x COLS (limites de compilacion, sin new/vtable), pero sin
// sc_module/sc_signal -- la malla es un arreglo plano de PE_MAC_State y
// mesh_step() reemplaza el port-binding hecho una sola vez en el
// constructor por una conexion explicita evaluada cada ciclo.
//
// Disciplina de registro: cada celda lee las salidas out_N/S/E/W de sus
// vecinos TAL COMO QUEDARON al final del ciclo anterior (un snapshot
// "viejo" tomado antes de correr ningun PE de este ciclo) -- mismo
// comportamiento que un flip-flop real entre celdas vecinas de la malla
// (out_* de un PE_MAC solo es visible al vecino en el ciclo siguiente,
// nunca en el mismo ciclo en que se escribe).
//
// Solo celdas PE_MAC (no la lista heterogenea de tipos de CGRA_Mesh_Static):
// GEMM 2x2 no necesita mezclar variantes de PE.
//
// mesh_program()/mesh_clear_acc(): version a nivel de malla de los canales
// laterales de pe_mac_program()/pe_mac_clear_acc() (PE_MAC_HLS_C.h) --
// resuelven la direccion de fila/columna de la PE y delegan. mesh_step() ya
// no recibe una grilla de cargas (load[ROWS][COLS]): programar y correr son
// caminos completamente separados ahora (ver cgra_hls_c/CGRA_Top_C.h).

#ifndef CGRA_MESH_STATIC_C_H
#define CGRA_MESH_STATIC_C_H

#include "../pe_hls_c/pe_isa_hls_c.h"
#include "../pe_hls_c/mac/PE_MAC_HLS_C.h"

template <int ROWS, int COLS, int DATA_W, int VLEN, int NUM_REGS = 8, int INSTR_MEM_SIZE = 4>
struct CGRA_Mesh_Static_C {
    typedef PE_VectorData<DATA_W, VLEN>                              Link;
    typedef PE_Instruction<DATA_W>                                   Instr;
    typedef PE_InstrIn<DATA_W>                                       InstrIn;
    typedef PE_MAC_State<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>      Cell;

    Cell pe[ROWS][COLS];
};

// Un ciclo de reloj de la malla completa: bound_in_N/in_S tienen tamano
// COLS (bordes norte/sur), bound_in_W/in_E tienen tamano ROWS (bordes
// oeste/este) -- mismo shape que los sc_vector<sc_in<Link>> de
// CGRA_Mesh_Static.h.
template <int ROWS, int COLS, int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void mesh_step(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& mesh,
                       bool rst, bool enable,
                       const PE_VectorData<DATA_W, VLEN> bound_in_N[COLS],
                       const PE_VectorData<DATA_W, VLEN> bound_in_S[COLS],
                       const PE_VectorData<DATA_W, VLEN> bound_in_W[ROWS],
                       const PE_VectorData<DATA_W, VLEN> bound_in_E[ROWS])
{
    typedef PE_VectorData<DATA_W, VLEN> Link;

    Link old_out_N[ROWS][COLS], old_out_S[ROWS][COLS];
    Link old_out_E[ROWS][COLS], old_out_W[ROWS][COLS];

snapshot_old_outputs:
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
            old_out_N[r][c] = mesh.pe[r][c].out_N;
            old_out_S[r][c] = mesh.pe[r][c].out_S;
            old_out_E[r][c] = mesh.pe[r][c].out_E;
            old_out_W[r][c] = mesh.pe[r][c].out_W;
        }
    }

step_all_cells:
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
            Link in_N = (r == 0)        ? bound_in_N[c] : old_out_S[r - 1][c];
            Link in_S = (r == ROWS - 1) ? bound_in_S[c] : old_out_N[r + 1][c];
            Link in_W = (c == 0)        ? bound_in_W[r] : old_out_E[r][c - 1];
            Link in_E = (c == COLS - 1) ? bound_in_E[r] : old_out_W[r][c + 1];

            pe_mac_step(mesh.pe[r][c], rst, enable, in_N, in_S, in_E, in_W);
        }
    }
}

// Escribe una unica instruccion en instr_mem[slot] de la PE (pe_row,pe_col).
// Canal lateral de configuracion -- no consume un ciclo de mesh_step, no
// pasa por el datapath de ejecucion.
template <int ROWS, int COLS, int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void mesh_program(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& mesh,
                          ap_uint<8> pe_row, ap_uint<8> pe_col, ap_uint<8> slot,
                          const PE_Instruction<DATA_W>& instr)
{
    pe_mac_program(mesh.pe[pe_row.to_uint() % ROWS][pe_col.to_uint() % COLS], slot, instr);
}

// Limpia el acumulador de las ROWS*COLS PEs, bypass de instr_mem.
template <int ROWS, int COLS, int DATA_W, int VLEN, int NUM_REGS, int INSTR_MEM_SIZE>
inline void mesh_clear_acc(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>& mesh)
{
clear_all_acc:
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
            pe_mac_clear_acc(mesh.pe[r][c]);
        }
    }
}

#endif // CGRA_MESH_STATIC_C_H
