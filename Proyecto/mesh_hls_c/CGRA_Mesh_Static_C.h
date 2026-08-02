// CGRA_Mesh_Static_C.h
// Malla heterogenea en C/C++ puro: cada posicion de la grilla puede ser un
// tipo de celda distinto (PE_MAC_State, PE_Scalar_State, PE_Vector_State,
// Routing_Cell_State, PE_Memory_State, ...), fijado por una lista de TIPOS
// en el template (CellTs...), igual espiritu que mesh_hls/CGRA_Mesh_Static.h
// (el tier SystemC-HLS que ya resuelve esto).
//
// Almacenamiento: una cadena de herencia recursiva (CellChain), NO
// std::tuple<CellTs...> -- se intento con std::tuple primero y
// csynth_design de Vitis HLS 2024.1 lo rechazo de plano: el frontend de
// sintesis no soporta los internos de <tuple>/<array>/<string> de esta
// libstdc++ ("C++ requires a type specifier for all declarations" en
// bits/stl_pair.h, bits/basic_string.h, array, tuple), aunque csim_design
// (g++ real) compilaba y corria sin problema -- mismo patron que "SystemC
// compila para csim pero no sintetiza" ya documentado para el bloqueo
// original de este proyecto. CellChain evita cualquier dependencia de STL:
// es un struct plano, sin necesitar la disciplina de sc_module_name que
// obligaba a esa forma en mesh_hls/CGRA_Mesh_Static.h -- aca es, ademas,
// la opcion mas segura para el frontend de sintesis.
//
// Contrato uniforme que cada CellT debe cumplir (por forma, no por
// herencia): campos `Link out_N, out_S, out_E, out_W` (Link =
// PE_VectorData<DATA_W,VLEN>) + overloads libres `cell_step(CellT&, bool
// rst, bool enable, Link,Link,Link,Link)`, `cell_program(CellT&, ap_uint<8>
// slot, const PE_Instruction<DATA_W>&)`, `cell_clear_acc(CellT&)` -- cada
// header de celda (PE_MAC_HLS_C.h, PE_Scalar_HLS_C.h, etc.) ya los aporta.
// Como la malla conoce el tipo concreto de cada posicion en tiempo de
// compilacion, estas llamadas resuelven al overload correcto por sobrecarga
// normal de C++, sin virtuales ni una clase de trait especializada a mano.
//
// mesh_program(mesh, pe_row, pe_col, slot, instr) recibe fila/columna en
// TIEMPO DE EJECUCION (el host puede programar cualquier celda), pero el
// acceso a CellChain exige un indice de TIEMPO DE COMPILACION -- se resuelve
// desenrollando las ROWS*COLS posiciones (fold expression) y comparando en
// runtime cual (r,c) coincide con (pe_row,pe_col); exactamente una posicion
// ejecuta la escritura. Es el mismo patron que un HLS usa para indexar en
// runtime un arreglo de tipos heterogeneos (un mux/case, no un puntero).
//
// Disciplina de registro sin cambios: cada celda lee las salidas
// out_N/S/E/W de sus vecinos TAL COMO QUEDARON al final del ciclo anterior
// (snapshot "viejo" tomado antes de correr a nadie este ciclo).

#ifndef CGRA_MESH_STATIC_C_H
#define CGRA_MESH_STATIC_C_H

#include <cstddef>
#include "../pe_hls_c/pe_isa_hls_c.h"

namespace cgra_mesh_static_c_detail {

// ---- Indices de tiempo de compilacion (sin <utility>, por las dudas) -----
template <std::size_t... Is>
struct index_seq {};

template <std::size_t N, std::size_t... Is>
struct make_index_seq_impl : make_index_seq_impl<N - 1, N - 1, Is...> {};

template <std::size_t... Is>
struct make_index_seq_impl<0, Is...> {
    typedef index_seq<Is...> type;
};

template <std::size_t N>
using make_index_seq = typename make_index_seq_impl<N>::type;

// ---- Almacenamiento heterogeneo: cadena de herencia, sin STL ------------
template <std::size_t I, typename... CellTs>
struct CellChain;

template <std::size_t I>
struct CellChain<I> {};

template <std::size_t I, typename Head, typename... Tail>
struct CellChain<I, Head, Tail...> : public CellChain<I + 1, Tail...> {
    Head cell;
};

template <std::size_t J, std::size_t I, typename... CellTs>
struct Getter;

template <std::size_t I, typename Head, typename... Tail>
struct Getter<I, I, Head, Tail...> {
    static Head& get(CellChain<I, Head, Tail...>& chain) { return chain.cell; }
};

template <std::size_t J, std::size_t I, typename Head, typename... Tail>
struct Getter<J, I, Head, Tail...> {
    static auto& get(CellChain<I, Head, Tail...>& chain) {
        return Getter<J, I + 1, Tail...>::get(chain);
    }
};

} // namespace cgra_mesh_static_c_detail

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
struct CGRA_Mesh_Static_C {
    static_assert(sizeof...(CellTs) == ROWS * COLS,
                  "CGRA_Mesh_Static_C: la lista de tipos de celda debe tener ROWS*COLS elementos");

    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;

    cgra_mesh_static_c_detail::CellChain<0, CellTs...> pe;

    // Acceso tipado a una celda por (fila,columna) en tiempo de compilacion,
    // mismo espiritu que .cell<I>() en mesh_hls/CGRA_Mesh_Static.h.
    template <int R, int C>
    auto& cell() { return cgra_mesh_static_c_detail::Getter<R * COLS + C, 0, CellTs...>::get(pe); }
};

namespace cgra_mesh_static_c_detail {

template <std::size_t I, int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void snapshot_one(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                          PE_VectorData<DATA_W, VLEN> old_out_N[ROWS][COLS],
                          PE_VectorData<DATA_W, VLEN> old_out_S[ROWS][COLS],
                          PE_VectorData<DATA_W, VLEN> old_out_E[ROWS][COLS],
                          PE_VectorData<DATA_W, VLEN> old_out_W[ROWS][COLS])
{
    constexpr int r = I / COLS;
    constexpr int c = I % COLS;
    auto& cell = Getter<I, 0, CellTs...>::get(mesh.pe);
    old_out_N[r][c] = cell.out_N;
    old_out_S[r][c] = cell.out_S;
    old_out_E[r][c] = cell.out_E;
    old_out_W[r][c] = cell.out_W;
}

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs, std::size_t... Is>
inline void snapshot_all(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                          PE_VectorData<DATA_W, VLEN> old_out_N[ROWS][COLS],
                          PE_VectorData<DATA_W, VLEN> old_out_S[ROWS][COLS],
                          PE_VectorData<DATA_W, VLEN> old_out_E[ROWS][COLS],
                          PE_VectorData<DATA_W, VLEN> old_out_W[ROWS][COLS],
                          index_seq<Is...>)
{
    // Fold expression: ya desenrollado en tiempo de compilacion por el
    // propio C++, no hace falta #pragma HLS UNROLL (no hay un for-loop que
    // Vitis HLS pueda ver aca).
    (snapshot_one<Is>(mesh, old_out_N, old_out_S, old_out_E, old_out_W), ...);
}

template <std::size_t I, int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void step_one(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                      const PE_VectorData<DATA_W, VLEN> old_out_N[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> old_out_S[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> old_out_E[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> old_out_W[ROWS][COLS],
                      bool rst, bool enable,
                      const PE_VectorData<DATA_W, VLEN> bound_in_N[COLS],
                      const PE_VectorData<DATA_W, VLEN> bound_in_S[COLS],
                      const PE_VectorData<DATA_W, VLEN> bound_in_W[ROWS],
                      const PE_VectorData<DATA_W, VLEN> bound_in_E[ROWS])
{
    constexpr int r = I / COLS;
    constexpr int c = I % COLS;
    typedef PE_VectorData<DATA_W, VLEN> Link;

    Link in_N = (r == 0)        ? bound_in_N[c] : old_out_S[r - 1][c];
    Link in_S = (r == ROWS - 1) ? bound_in_S[c] : old_out_N[r + 1][c];
    Link in_W = (c == 0)        ? bound_in_W[r] : old_out_E[r][c - 1];
    Link in_E = (c == COLS - 1) ? bound_in_E[r] : old_out_W[r][c + 1];

    cell_step(Getter<I, 0, CellTs...>::get(mesh.pe), rst, enable, in_N, in_S, in_E, in_W);
}

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs, std::size_t... Is>
inline void step_all(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                      const PE_VectorData<DATA_W, VLEN> old_out_N[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> old_out_S[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> old_out_E[ROWS][COLS],
                      const PE_VectorData<DATA_W, VLEN> old_out_W[ROWS][COLS],
                      bool rst, bool enable,
                      const PE_VectorData<DATA_W, VLEN> bound_in_N[COLS],
                      const PE_VectorData<DATA_W, VLEN> bound_in_S[COLS],
                      const PE_VectorData<DATA_W, VLEN> bound_in_W[ROWS],
                      const PE_VectorData<DATA_W, VLEN> bound_in_E[ROWS],
                      index_seq<Is...>)
{
    (step_one<Is>(mesh, old_out_N, old_out_S, old_out_E, old_out_W, rst, enable,
                  bound_in_N, bound_in_S, bound_in_W, bound_in_E), ...);
}

template <std::size_t I, int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void program_one(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                         ap_uint<8> pe_row, ap_uint<8> pe_col, ap_uint<8> slot,
                         const PE_Instruction<DATA_W>& instr)
{
    constexpr int r = I / COLS;
    constexpr int c = I % COLS;
    if (pe_row.to_uint() % ROWS == static_cast<unsigned>(r) &&
        pe_col.to_uint() % COLS == static_cast<unsigned>(c)) {
        cell_program(Getter<I, 0, CellTs...>::get(mesh.pe), slot, instr);
    }
}

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs, std::size_t... Is>
inline void program_all(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                         ap_uint<8> pe_row, ap_uint<8> pe_col, ap_uint<8> slot,
                         const PE_Instruction<DATA_W>& instr, index_seq<Is...>)
{
    (program_one<Is>(mesh, pe_row, pe_col, slot, instr), ...);
}

template <std::size_t I, int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void clear_acc_one(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh) {
    cell_clear_acc(Getter<I, 0, CellTs...>::get(mesh.pe));
}

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs, std::size_t... Is>
inline void clear_acc_all(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                           index_seq<Is...>)
{
    (clear_acc_one<Is>(mesh), ...);
}

} // namespace cgra_mesh_static_c_detail

// Un ciclo de reloj de la malla completa: bound_in_N/in_S tienen tamano
// COLS (bordes norte/sur), bound_in_W/in_E tienen tamano ROWS (bordes
// oeste/este) -- mismo shape que antes de generalizar a heterogenea.
template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void mesh_step(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                       bool rst, bool enable,
                       const PE_VectorData<DATA_W, VLEN> bound_in_N[COLS],
                       const PE_VectorData<DATA_W, VLEN> bound_in_S[COLS],
                       const PE_VectorData<DATA_W, VLEN> bound_in_W[ROWS],
                       const PE_VectorData<DATA_W, VLEN> bound_in_E[ROWS])
{
    typedef PE_VectorData<DATA_W, VLEN> Link;

    Link old_out_N[ROWS][COLS], old_out_S[ROWS][COLS];
    Link old_out_E[ROWS][COLS], old_out_W[ROWS][COLS];

    cgra_mesh_static_c_detail::snapshot_all(mesh, old_out_N, old_out_S, old_out_E, old_out_W,
                                             cgra_mesh_static_c_detail::make_index_seq<sizeof...(CellTs)>{});
    cgra_mesh_static_c_detail::step_all(mesh, old_out_N, old_out_S, old_out_E, old_out_W, rst, enable,
                                         bound_in_N, bound_in_S, bound_in_W, bound_in_E,
                                         cgra_mesh_static_c_detail::make_index_seq<sizeof...(CellTs)>{});
}

// Escribe una unica instruccion/config en la celda (pe_row,pe_col). Canal
// lateral de configuracion -- no consume un ciclo de mesh_step.
template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void mesh_program(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                          ap_uint<8> pe_row, ap_uint<8> pe_col, ap_uint<8> slot,
                          const PE_Instruction<DATA_W>& instr)
{
    cgra_mesh_static_c_detail::program_all(mesh, pe_row, pe_col, slot, instr,
                                            cgra_mesh_static_c_detail::make_index_seq<sizeof...(CellTs)>{});
}

// Limpia el acumulador de las ROWS*COLS celdas (no-op en las que no tengan).
template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void mesh_clear_acc(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh)
{
    cgra_mesh_static_c_detail::clear_acc_all(mesh, cgra_mesh_static_c_detail::make_index_seq<sizeof...(CellTs)>{});
}

// Vuelca out_N/S/E/W de las ROWS*COLS celdas en arreglos Link[ROWS][COLS]
// homogeneos -- util para leer los bordes de la malla desde fuera (p.ej. el
// estado DONE de cgra_run, ver cgra_hls_c/CGRA_Top_C.h) sin necesitar un
// indice de tiempo de compilacion por celda: una vez volcado, el acceso es
// indexado en runtime como cualquier arreglo comun.
template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
inline void mesh_read_outputs(CGRA_Mesh_Static_C<ROWS, COLS, DATA_W, VLEN, CellTs...>& mesh,
                               PE_VectorData<DATA_W, VLEN> out_N[ROWS][COLS],
                               PE_VectorData<DATA_W, VLEN> out_S[ROWS][COLS],
                               PE_VectorData<DATA_W, VLEN> out_E[ROWS][COLS],
                               PE_VectorData<DATA_W, VLEN> out_W[ROWS][COLS])
{
    cgra_mesh_static_c_detail::snapshot_all(mesh, out_N, out_S, out_E, out_W,
                                             cgra_mesh_static_c_detail::make_index_seq<sizeof...(CellTs)>{});
}

#endif // CGRA_MESH_STATIC_C_H
