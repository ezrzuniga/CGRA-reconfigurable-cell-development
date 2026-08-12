// NoC_Mesh_Static_C.h
// Transliteracion a C/C++ puro de cgra_final_noc/NoC_Mesh_Static.h: version
// NoC (packet-switched) de mesh_hls_c/CGRA_Mesh_Static_C.h. Mismo API publico
// (programar una celda, limpiar acumuladores, cell<R,C>(), un paso de reloj,
// mismos bordes externos de tipo Link, mismo contrato ROWS*COLS de tipos de
// celda por template), pero el tejido interno que conecta las ROWS*COLS celdas
// ya no son wires punto a punto: es una malla de routers (uno por posicion,
// ver NoC_Router_C.h) que reenvian paquetes con header dest_row/dest_col en
// vez de leer un wire fijo.
//
// Reusa (sin modificar) la maquinaria de almacenamiento heterogeneo de
// mesh_hls_c/CGRA_Mesh_Static_C.h en vez de reimplementarla: la malla NoC
// CONTIENE una CGRA_Mesh_Static_C (`core`) y le delega el CellChain, el
// dispatch por sobrecarga de cell_step/cell_program/cell_clear_acc y las
// utilidades de programacion/lectura. Lo unico propio de este header es el
// paso de reloj, que es donde vive la diferencia real (routers en vez de
// wires). Los routers no aportan estado: son funciones puras (ver
// NoC_Router_C.h), asi que la malla NoC no tiene ni un bit de estado mas que
// la malla directa -- exactamente la misma huella de registros.
//
// Cada celda de computo sigue exponiendo EXACTAMENTE el mismo contrato de 4
// puertos que ya tenia -- no se toca ninguna celda existente. Lo que cambia es
// a que se conectan: en vez de ir directo al vecino (o al borde externo), van
// al puerto LOCAL del router en su misma posicion (celda.out_X ->
// router.in_L_X, router.out_L_X -> celda.in_X).
//
// PUENTE DE BORDE (Link <-> NoC_Packet_C): los puertos externos de esta malla
// siguen siendo Link liso para que un testbench que ya sabe manejar
// CGRA_Mesh_Static_C pueda manejar esta version sin cambiar un tipo. El header
// que se le asigna a un paquete que entra por un borde externo es siempre la
// posicion del PROPIO router de entrada (dest = su (row,col)): por
// construccion, un dato que en la malla de wires directos llegaba directamente
// al puerto in_X de la celda en esa posicion de borde debe seguir llegando a
// esa MISMA celda aca.
//
// EQUIVALENCIA CICLO A CICLO CON LA MALLA DIRECTA -- el punto de todo esto, y
// la parte que en C hay que construir a proposito (en SystemC salia gratis
// porque el kernel iteraba delta-ciclos hasta converger, y los routers, al ser
// SC_METHOD combinacionales, se re-evaluaban solos hasta estabilizarse):
//
//   noc_mesh_step() evalua la fabrica de routers NUM_PASSES veces DENTRO del
//   mismo paso de reloj, encadenando la salida de malla de cada router a la
//   entrada de sus vecinos en la pasada siguiente. NUM_PASSES = ROWS+COLS-1 es
//   el diametro XY de la malla (peor caso: (COLS-1) saltos en columna +
//   (ROWS-1) en fila, mas la inyeccion inicial), asi que la cadena
//   combinacional SIEMPRE queda resuelta dentro del paso -- igual que el
//   punto fijo al que convergia el kernel de SystemC, pero con una cota de
//   tiempo de compilacion, que es lo que Vitis HLS necesita para sintetizar.
//
//   Con eso, los dos caminos que existen en la malla directa quedan calcados:
//     - celda -> celda vecina: la celda origen escribio su out_X el paso
//       ANTERIOR (snapshot, misma disciplina que CGRA_Mesh_Static_C), su
//       router lo inyecta en la pasada 1 y el router vecino lo entrega en la
//       pasada 2 -> la celda destino lo lee este paso. 1 ciclo, igual que el
//       wire directo.
//     - borde externo -> celda de borde: el paquete de borde nace con dest =
//       el propio router, que lo entrega localmente ya en la pasada 1 -> se ve
//       en el mismo paso en que se escribe. 0 ciclos, igual que el wire
//       directo.
//
//   Con el trafico real de este repo (siempre 0 o 1 salto, ver NoC_Router_C.h)
//   la fabrica ya esta estable en la pasada 2 y las pasadas restantes no
//   cambian nada -- estan para que la malla siga siendo correcta si alguna vez
//   se inyecta trafico multi-hop de verdad (como hace NoC_Router_C__TB.cpp
//   sobre routers sueltos).
//
//   Segundo punto de muestreo, y el detalle mas facil de pasar por alto al
//   portar esto a C: la fabrica es COMBINACIONAL, asi que dentro de un mismo
//   ciclo se la "mira" en DOS momentos distintos. Antes del flanco, para
//   saber que entra a cada celda (usa las salidas de celda del ciclo
//   anterior); despues del flanco, para saber que sale por los bordes
//   externos (usa las salidas de celda RECIEN escritas). En SystemC eso salia
//   solo -- el kernel re-evaluaba route() cuando cambiaba cualquier entrada,
//   incluidas las salidas de celda recien actualizadas, y los puertos de borde
//   quedaban con el valor final del time step. En C hay que escribirlo: por eso
//   noc_mesh_step() llama a noc_fabric_eval() dos veces, una antes y una
//   despues de correr las celdas. No son dos fabricas: es la misma, muestreada
//   en dos instantes del ciclo (en la malla directa el equivalente es que
//   "la salida del borde" sea el campo out_X de la celda leido DESPUES del
//   paso -- camino de 0 ciclos celda->borde, identico aca).

#ifndef NOC_MESH_STATIC_C_H
#define NOC_MESH_STATIC_C_H

#include <cstddef>
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"  // reusa CellChain/Getter/index_seq y mesh_program/...
#include "NoC_Packet_C.h"
#include "NoC_Router_C.h"

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
struct NoC_Mesh_Static_C {
    static_assert(sizeof...(CellTs) == ROWS * COLS,
                  "NoC_Mesh_Static_C: la lista de tipos de celda debe tener ROWS*COLS elementos");

    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;
    typedef NoC_Packet_C<DATA_W, VLEN>  Packet;

    // Diametro XY de la malla: cuantas pasadas combinacionales hacen falta
    // para que la fabrica quede resuelta dentro de un paso de reloj.
    static const int NUM_PASSES = ROWS + COLS - 1;

    CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...> core;

    template <int R, int C>
    auto& cell() { return core.template cell<R, C>(); }
};

namespace noc_mesh_static_c_detail {

using cgra_mesh_static_c_detail::Getter;
using cgra_mesh_static_c_detail::index_seq;
using cgra_mesh_static_c_detail::make_index_seq;

// Corre una celda con los 4 Link que su router le entrego este paso.
template <std::size_t I, int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void step_one(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& core,
                      bool rst, bool enable,
                      const PE_VectorData<DATA_W, VLEN> in_N[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> in_S[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> in_E[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> in_W[ROWS][COLS])
{
    constexpr int r = I / COLS;
    constexpr int c = I % COLS;
    cell_step(Getter<I, 0, CellTs...>::get(core.pe), rst, enable,
              in_N[r][c], in_S[r][c], in_E[r][c], in_W[r][c]);
}

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs, std::size_t... Is>
inline void step_all(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& core,
                      bool rst, bool enable,
                      const PE_VectorData<DATA_W, VLEN> in_N[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> in_S[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> in_E[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> in_W[ROWS][COLS],
                      index_seq<Is...>)
{
    (step_one<Is>(core, rst, enable, in_N, in_S, in_E, in_W), ...);
}

// Resuelve la fabrica de routers completa para un juego dado de inyecciones
// locales (las 4 salidas de cada celda) y de paquetes de borde. Es la unica
// pieza combinacional de la malla NoC, y se la muestrea dos veces por ciclo
// (ver comentario de cabecera).
template <int ROWS, int COLS, int NUM_PASSES, int DATA_W, int VLEN>
inline void fabric_eval(bool rst, bool enable,
                         const PE_VectorData<DATA_W, VLEN> inj_N[ROWS][COLS],
                         const PE_VectorData<DATA_W, VLEN> inj_S[ROWS][COLS],
                         const PE_VectorData<DATA_W, VLEN> inj_E[ROWS][COLS],
                         const PE_VectorData<DATA_W, VLEN> inj_W[ROWS][COLS],
                         const NoC_Packet_C<DATA_W, VLEN> bnd_N[COLS],
                         const NoC_Packet_C<DATA_W, VLEN> bnd_S[COLS],
                         const NoC_Packet_C<DATA_W, VLEN> bnd_W[ROWS],
                         const NoC_Packet_C<DATA_W, VLEN> bnd_E[ROWS],
                         NoC_RouterOut_C<DATA_W, VLEN> ro[ROWS][COLS])
{
    typedef NoC_Packet_C<DATA_W, VLEN> Packet;

    // prev_* son las salidas de malla de la pasada anterior; en la primera
    // pasada valen "invalido" (ningun vecino mando nada todavia), que es justo
    // el estado inicial correcto.
    Packet prev_N[ROWS][COLS], prev_S[ROWS][COLS], prev_E[ROWS][COLS], prev_W[ROWS][COLS];
#pragma HLS ARRAY_PARTITION variable=prev_N complete dim=0
#pragma HLS ARRAY_PARTITION variable=prev_S complete dim=0
#pragma HLS ARRAY_PARTITION variable=prev_E complete dim=0
#pragma HLS ARRAY_PARTITION variable=prev_W complete dim=0

noc_pass_loop:
    for (int pass = 0; pass < NUM_PASSES; pass++) {
#pragma HLS UNROLL
    noc_router_rows:
        for (int r = 0; r < ROWS; r++) {
#pragma HLS UNROLL
        noc_router_cols:
            for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
                // Enlace de malla o puente de borde, con el mismo indexado por
                // posicion que CGRA_Mesh_Static_C: el in_N de (r,c) es el
                // out_S del router de arriba, etc.
                Packet in_N = (r == 0)        ? bnd_N[c] : prev_S[r - 1][c];
                Packet in_S = (r == ROWS - 1) ? bnd_S[c] : prev_N[r + 1][c];
                Packet in_W = (c == 0)        ? bnd_W[r] : prev_E[r][c - 1];
                Packet in_E = (c == COLS - 1) ? bnd_E[r] : prev_W[r][c + 1];

                noc_router_route<DATA_W, VLEN>(r, c, rst, enable,
                                               in_N, in_S, in_E, in_W,
                                               inj_N[r][c], inj_S[r][c], inj_E[r][c], inj_W[r][c],
                                               ro[r][c]);
            }
        }
        // Las salidas de esta pasada son las entradas de la siguiente.
    noc_pass_commit:
        for (int r = 0; r < ROWS; r++) {
#pragma HLS UNROLL
            for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
                prev_N[r][c] = ro[r][c].mesh_N;
                prev_S[r][c] = ro[r][c].mesh_S;
                prev_E[r][c] = ro[r][c].mesh_E;
                prev_W[r][c] = ro[r][c].mesh_W;
            }
        }
    }
}

} // namespace noc_mesh_static_c_detail

// ---- Canales laterales: delegados tal cual a la malla directa -------------
template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void noc_mesh_program(NoC_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                              ap_uint<8> pe_row, ap_uint<8> pe_col, ap_uint<8> slot,
                              const PE_Instruction<DATA_W>& instr)
{
    mesh_program(mesh.core, pe_row, pe_col, slot, instr);
}

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void noc_mesh_clear_acc(NoC_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh)
{
    mesh_clear_acc(mesh.core);
}

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void noc_mesh_read_outputs(NoC_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                                   PE_VectorData<DATA_W, VLEN> out_N[ROWS][COLS],
                                   PE_VectorData<DATA_W, VLEN> out_S[ROWS][COLS],
                                   PE_VectorData<DATA_W, VLEN> out_E[ROWS][COLS],
                                   PE_VectorData<DATA_W, VLEN> out_W[ROWS][COLS])
{
    mesh_read_outputs(mesh.core, out_N, out_S, out_E, out_W);
}

// ---- Un ciclo de reloj de la malla NoC completa ---------------------------
// bound_in_N/in_S tienen tamano COLS; bound_in_W/in_E tamano ROWS -- mismo
// shape que mesh_step(). A diferencia de la malla directa, aca los bordes de
// SALIDA si son parametros explicitos: en la malla directa "la salida del
// borde" era literalmente el campo out_X de la celda de borde, mientras que
// aca el ultimo tramo lo maneja el router de esa posicion (que ademas es quien
// descarta el header).
template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void noc_mesh_step(NoC_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                           bool rst, bool enable,
                           const PE_VectorData<DATA_W, VLEN> bound_in_N[COLS],
                           const PE_VectorData<DATA_W, VLEN> bound_in_S[COLS],
                           const PE_VectorData<DATA_W, VLEN> bound_in_W[ROWS],
                           const PE_VectorData<DATA_W, VLEN> bound_in_E[ROWS],
                           PE_VectorData<DATA_W, VLEN> bound_out_N[COLS],
                           PE_VectorData<DATA_W, VLEN> bound_out_S[COLS],
                           PE_VectorData<DATA_W, VLEN> bound_out_W[ROWS],
                           PE_VectorData<DATA_W, VLEN> bound_out_E[ROWS])
{
    using namespace noc_mesh_static_c_detail;
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef NoC_Packet_C<DATA_W, VLEN>  Packet;
    typedef NoC_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...> MeshT;

    // 1) Puente de borde de entrada: cada Link externo se envuelve con
    //    dest = posicion del router de borde que lo recibe. Vale para las dos
    //    evaluaciones de la fabrica (los bordes no cambian dentro del ciclo).
    Packet bnd_N[COLS], bnd_S[COLS], bnd_W[ROWS], bnd_E[ROWS];
#pragma HLS ARRAY_PARTITION variable=bnd_N complete dim=0
#pragma HLS ARRAY_PARTITION variable=bnd_S complete dim=0
#pragma HLS ARRAY_PARTITION variable=bnd_W complete dim=0
#pragma HLS ARRAY_PARTITION variable=bnd_E complete dim=0
bridge_in_cols:
    for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
        bnd_N[c].valid = true; bnd_N[c].dest_row = 0;        bnd_N[c].dest_col = c; bnd_N[c].data = bound_in_N[c];
        bnd_S[c].valid = true; bnd_S[c].dest_row = ROWS - 1; bnd_S[c].dest_col = c; bnd_S[c].data = bound_in_S[c];
    }
bridge_in_rows:
    for (int r = 0; r < ROWS; r++) {
#pragma HLS UNROLL
        bnd_W[r].valid = true; bnd_W[r].dest_row = r; bnd_W[r].dest_col = 0;        bnd_W[r].data = bound_in_W[r];
        bnd_E[r].valid = true; bnd_E[r].dest_row = r; bnd_E[r].dest_col = COLS - 1; bnd_E[r].data = bound_in_E[r];
    }

    Link inj_N[ROWS][COLS], inj_S[ROWS][COLS], inj_E[ROWS][COLS], inj_W[ROWS][COLS];
#pragma HLS ARRAY_PARTITION variable=inj_N complete dim=0
#pragma HLS ARRAY_PARTITION variable=inj_S complete dim=0
#pragma HLS ARRAY_PARTITION variable=inj_E complete dim=0
#pragma HLS ARRAY_PARTITION variable=inj_W complete dim=0
    NoC_RouterOut_C<DATA_W, VLEN> ro[ROWS][COLS];
#pragma HLS ARRAY_PARTITION variable=ro complete dim=0

    // 2) PRIMER MUESTREO de la fabrica (antes del flanco): las inyecciones
    //    locales son las salidas que cada celda dejo escritas el paso anterior
    //    -- misma disciplina de registro que CGRA_Mesh_Static_C. De aca sale lo
    //    que cada celda va a leer en sus 4 puertos este paso.
    mesh_read_outputs(mesh.core, inj_N, inj_S, inj_E, inj_W);
    fabric_eval<ROWS, COLS, MeshT::NUM_PASSES, DATA_W, VLEN>(
        rst, enable, inj_N, inj_S, inj_E, inj_W, bnd_N, bnd_S, bnd_W, bnd_E, ro);

    Link cin_N[ROWS][COLS], cin_S[ROWS][COLS], cin_E[ROWS][COLS], cin_W[ROWS][COLS];
#pragma HLS ARRAY_PARTITION variable=cin_N complete dim=0
#pragma HLS ARRAY_PARTITION variable=cin_S complete dim=0
#pragma HLS ARRAY_PARTITION variable=cin_E complete dim=0
#pragma HLS ARRAY_PARTITION variable=cin_W complete dim=0
deliver_rows:
    for (int r = 0; r < ROWS; r++) {
#pragma HLS UNROLL
        for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
            cin_N[r][c] = ro[r][c].local_N;
            cin_S[r][c] = ro[r][c].local_S;
            cin_E[r][c] = ro[r][c].local_E;
            cin_W[r][c] = ro[r][c].local_W;
        }
    }

    // 3) Flanco de reloj: las 9 celdas corren con lo que su router les
    //    entrego localmente.
    step_all(mesh.core, rst, enable, cin_N, cin_S, cin_E, cin_W,
             cgra_mesh_static_c_detail::make_index_seq<sizeof...(CellTs)>{});

    // 4) SEGUNDO MUESTREO de la fabrica (despues del flanco), con las salidas
    //    de celda RECIEN escritas: de aca salen los bordes externos. Es la
    //    misma fabrica combinacional vista en otro instante del ciclo, no una
    //    segunda copia conceptual -- ver comentario de cabecera. Un top de
    //    sintesis que no necesite leer los bordes puede saltear este paso
    //    entero (y ahorrarse la logica) llamando solo a los pasos 1-3.
    mesh_read_outputs(mesh.core, inj_N, inj_S, inj_E, inj_W);
    fabric_eval<ROWS, COLS, MeshT::NUM_PASSES, DATA_W, VLEN>(
        rst, enable, inj_N, inj_S, inj_E, inj_W, bnd_N, bnd_S, bnd_W, bnd_E, ro);

    // 5) Puente de borde de salida: se desenvuelve el payload hacia el Link
    //    externo (el header se descarta -- ya cumplio su proposito).
bridge_out_cols:
    for (int c = 0; c < COLS; c++) {
#pragma HLS UNROLL
        bound_out_N[c] = ro[0][c].mesh_N.data;
        bound_out_S[c] = ro[ROWS - 1][c].mesh_S.data;
    }
bridge_out_rows:
    for (int r = 0; r < ROWS; r++) {
#pragma HLS UNROLL
        bound_out_W[r] = ro[r][0].mesh_W.data;
        bound_out_E[r] = ro[r][COLS - 1].mesh_E.data;
    }
}

#endif // NOC_MESH_STATIC_C_H
