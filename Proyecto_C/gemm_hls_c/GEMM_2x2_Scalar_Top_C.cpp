// GEMM_2x2_Scalar_Top_C.cpp
// Definicion de GEMM_2x2_Scalar_Top_C (ver el .h para el diseno completo).
// Separada del header por el mismo motivo que GEMM_2x2_HLS_Top_C.cpp: Vitis
// HLS no encuentra un top marcado `inline` ("ERROR: [HLS 214-157] Top function
// not found").

#include "GEMM_2x2_Scalar_Top_C.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

namespace {

// Carga el programa espacial de GEMM en las 4 celdas. Se accede a cada celda
// con indices de TIEMPO DE COMPILACION (cell<R,C>()), no via mesh_program(),
// que resolveria (row,col) en runtime con un mux de 4 vias -- aca las 4
// posiciones se conocen, asi que el mux no hace falta y no se paga.
inline void load_fixed_gemm_program(GemmMesh_C& mesh) {
#pragma HLS INLINE
    GemmInstr_C prog[GEMM_ROWS][GEMM_COLS][GEMM_INSTR_MEM_SIZE];
#pragma HLS ARRAY_PARTITION variable=prog complete dim=0
    gemm_program_c(prog);

gemm_boot_load_loop:
    for (int slot = 0; slot < GEMM_INSTR_MEM_SIZE; slot++) {
#pragma HLS UNROLL
        cell_program(mesh.cell<0, 0>(), slot, prog[0][0][slot]);
        cell_program(mesh.cell<0, 1>(), slot, prog[0][1][slot]);
        cell_program(mesh.cell<1, 0>(), slot, prog[1][0][slot]);
        cell_program(mesh.cell<1, 1>(), slot, prog[1][1][slot]);
    }
}

inline GemmLink_C scalar_link(ap_int<GEMM_DATA_W> v) {
#pragma HLS INLINE
    GemmLink_C l;
    l[0] = v;
    return l;
}

} // namespace

void GEMM_2x2_Scalar_Top_C(
    bool start, bool& done,
    ap_int<GEMM_DATA_W> a00, ap_int<GEMM_DATA_W> a01,
    ap_int<GEMM_DATA_W> a10, ap_int<GEMM_DATA_W> a11,
    ap_int<GEMM_DATA_W> b00, ap_int<GEMM_DATA_W> b01,
    ap_int<GEMM_DATA_W> b10, ap_int<GEMM_DATA_W> b11,
    ap_int<GEMM_DATA_W>& c00, ap_int<GEMM_DATA_W>& c01,
    ap_int<GEMM_DATA_W>& c10, ap_int<GEMM_DATA_W>& c11)
{
    // Unico estado con memoria del diseno. Es `static` por la misma razon que
    // en GEMM_2x2_HLS_Top_C (los registros de la malla tienen que sobrevivir
    // entre invocaciones), aunque aca el programa se recarga en cada llamada:
    // lo que importa que persista es la estructura, no el contenido.
    static GemmMesh_C mesh;

    // Programa fijo: se reescribe en cada invocacion en vez de guardarse un bit
    // de "ya booteado". Cuesta 0 ciclos (canal lateral, ver PE_MAC_HLS_C.h) y a
    // cambio el top queda sin estado de arranque -- una invocacion despues del
    // reset del sistema da el mismo resultado que la primera, sin depender de
    // que la secuencia BOOT_LOAD del original haya corrido alguna vez.
    load_fixed_gemm_program(mesh);

    if (!start) {
        done = false;
        return;
    }

    // A entra por in_W[fase][fila] = A[fila][fase] (columna `fase` de A);
    // B entra por in_N[fase][columna] = B[fase][columna] (fila `fase` de B).
    // in_S/in_E no los usa el programa de GEMM -- quedan en cero.
    GemmLink_C in_N[GEMM_NUM_PHASES][GEMM_COLS], in_S[GEMM_NUM_PHASES][GEMM_COLS];
    GemmLink_C in_W[GEMM_NUM_PHASES][GEMM_ROWS], in_E[GEMM_NUM_PHASES][GEMM_ROWS];
#pragma HLS ARRAY_PARTITION variable=in_N complete dim=0
#pragma HLS ARRAY_PARTITION variable=in_S complete dim=0
#pragma HLS ARRAY_PARTITION variable=in_W complete dim=0
#pragma HLS ARRAY_PARTITION variable=in_E complete dim=0

    in_W[0][0] = scalar_link(a00);  in_W[0][1] = scalar_link(a10);   // fase k=0: columna 0 de A
    in_W[1][0] = scalar_link(a01);  in_W[1][1] = scalar_link(a11);   // fase k=1: columna 1 de A
    in_N[0][0] = scalar_link(b00);  in_N[0][1] = scalar_link(b01);   // fase k=0: fila 0 de B
    in_N[1][0] = scalar_link(b10);  in_N[1][1] = scalar_link(b11);   // fase k=1: fila 1 de B

    GemmLink_C out_N[GEMM_COLS], out_S[GEMM_COLS];
    GemmLink_C out_W[GEMM_ROWS], out_E[GEMM_ROWS];
#pragma HLS ARRAY_PARTITION variable=out_N complete dim=0
#pragma HLS ARRAY_PARTITION variable=out_S complete dim=0
#pragma HLS ARRAY_PARTITION variable=out_W complete dim=0
#pragma HLS ARRAY_PARTITION variable=out_E complete dim=0

    GemmInstr_C unused_instr;  // prog_valid=false -- nunca se lee
    cgra_run<GEMM_ROWS, GEMM_COLS, GEMM_DATA_W, GEMM_VLEN, GEMM_INSTR_MEM_SIZE, GEMM_NUM_PHASES>(
        mesh, /*prog_valid=*/false, 0, 0, 0, unused_instr, /*start=*/true, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

    // C[0][0] y C[1][0] salen por el borde oeste; C[0][1] y C[1][1] por el este
    // (ver el mapa de puertos en GEMM_2x2_Mesh_C.h).
    c00 = out_W[0][0];
    c01 = out_E[0][0];
    c10 = out_W[1][0];
    c11 = out_E[1][0];
}
