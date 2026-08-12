// NoC_Router_C.h
// Transliteracion a C/C++ puro de cgra_final_noc/NoC_Router.h: mismo contrato
// de 8 puertos de dato que Routing_Cell_HLS_C.h ampliado a 8 (4 "de malla"
// N/S/E/W hacia routers vecinos + 4 "locales" L_N/L_S/L_E/L_W hacia la celda
// de computo adjunta, espejo 1:1: celda.out_X <-> router.in_L_X,
// router.out_L_X <-> celda.in_X), pero en vez de un mux fijado por
// configuracion estatica (RC_Config), la salida se decide EN CADA CICLO a
// partir del header (dest_row,dest_col) que cada paquete lleva encima.
//
// Estructura en C: el router del original era un sc_module sin estado propio
// (SC_METHOD combinacional puro). Aca eso se vuelve literalmente una FUNCION
// PURA, noc_router_route(), sin ningun struct de estado que le corresponda --
// las "salidas" son un NoC_RouterOut_C de salida por referencia. Es la
// traduccion mas directa posible de "no tiene estado propio ni flancos de
// reloj": en C esa propiedad se puede expresar en el tipo, no solo en un
// comentario. row/col dejan de ser miembros fijados en el constructor y pasan
// a ser argumentos -- en la malla estatica son constantes de tiempo de
// compilacion, asi que Vitis HLS las propaga y el mux resultante es el mismo.
//
// Algoritmo de ruteo: XY (dimension-order), columna primero, fila despues --
// el mismo algoritmo textbook que usan la mayoria de NoCs de malla 2D porque
// es libre de deadlock sin necesidad de virtual channels. Un router en
// (row,col) que ve un paquete con destino (dr,dc):
//   1. si dc != col: el paquete sigue moviendose en columna (out_E si dc >
//      col, out_W si dc < col).
//   2. si dc == col pero dr != row: recien ahora se permite "doblar" a fila
//      (out_S si dr > row, out_N si dr < row -- la fila crece hacia el sur).
//   3. si dc == col y dr == row: el paquete llego -- se entrega al puerto
//      local que mira hacia la direccion por la que llego (in_N -> out_L_N,
//      etc.), nunca a un puerto local distinto.
//
// Inyeccion local (in_L_N/S/E/W, tipo Link -- NO Packet: la celda adjunta no
// sabe nada de paquetes, solo escribe su out_N/S/E/W de siempre): por
// construccion del ISA de este repo (pe_isa_hls_c.h, SRC_/DST_NORTH|SOUTH|
// EAST|WEST) una celda SOLO puede dirigirse a su vecino INMEDIATO -- nunca hay
// una direccion de "2 saltos" en una instruccion. Por eso el router arma el
// header de un paquete inyectado localmente con su PROPIA posicion +-1 en el
// eje correspondiente (in_L_N -> dest=(row-1,col), etc.): siempre exactamente
// 1 salto, calculado aca. Como Link no tiene bit de validez (la celda adjunta
// siempre esta manejando algo en sus 4 puertos), el paquete inyectado se marca
// valid=true incondicionalmente cada ciclo. Si el salto calculado cae fuera de
// la malla (borde), el dest resultante nunca se vuelve a leer -- el paquete
// sale directo por el puerto de malla de este mismo router hacia el borde
// externo (ver NoC_Mesh_Static_C.h), donde el header se descarta.
// Consecuencia importante: con el trafico que genera una celda de computo real
// (siempre 1 salto), un paquete SIEMPRE se entrega en el primer router al que
// llega -- nunca hay "transito" (2+ saltos) mas que en pruebas sinteticas que
// inyectan un header lejano a mano (NoC_Router_C__TB.cpp).
//
// Arbitraje: solo puede haber contienda real en los 4 puertos de malla
// (out_N/S/E/W) -- la entrega local nunca compite, ver arriba -- y solo entre
// la inyeccion local que apunta a esa direccion, el trafico recto que sigue de
// largo, y el trafico que recien esta "doblando" desde el otro eje. Prioridad
// fija: trafico en transito (recto y doblando) antes que inyeccion local nueva
// -- evita que un paquete que ya viaja hace 1+ saltos quede indefinidamente
// bloqueado por inyecciones frescas. Sin buffers ni credit-based flow control:
// el candidato que pierde el arbitraje se descarta ese ciclo -- limitacion
// conocida, documentada aca en vez de escondida; trabajo futuro razonable es
// agregar FIFOs de entrada + backpressure (hls::stream con profundidad, ya que
// esta version si es sintetizable). Con el trafico real de este repo esta
// contienda NUNCA ocurre.
//
// rst, igual que enable=false, fuerza las 8 salidas a su valor por defecto
// (Packet invalido / Link en cero) por seguridad y determinismo.

#ifndef NOC_ROUTER_C_H
#define NOC_ROUTER_C_H

#include "NoC_Packet_C.h"

// Las 8 salidas de un router: 4 de malla (con header) + 4 locales (Link liso
// hacia la celda de computo adjunta, ya sin header).
template <int DATA_W = 32, int VLEN = 4>
struct NoC_RouterOut_C {
    NoC_Packet_C<DATA_W, VLEN>  mesh_N, mesh_S, mesh_E, mesh_W;
    PE_VectorData<DATA_W, VLEN> local_N, local_S, local_E, local_W;
};

namespace noc_router_c_detail {

enum NextHop { HOP_LOCAL = 0, HOP_N, HOP_S, HOP_E, HOP_W };

// XY routing: columna primero, fila despues (ver comentario de cabecera).
template <int DATA_W, int VLEN>
inline NextHop next_hop(const NoC_Packet_C<DATA_W, VLEN>& p, int row, int col) {
    if (!p.valid) return HOP_LOCAL;  // paquete invalido: nunca compite por nada
    int dr = (int)p.dest_row.to_uint();
    int dc = (int)p.dest_col.to_uint();
    if (dc > col) return HOP_E;
    if (dc < col) return HOP_W;
    if (dr > row) return HOP_S;
    if (dr < row) return HOP_N;
    return HOP_LOCAL;
}

// La celda adjunta siempre esta manejando algo en su out_X (sin bit de
// validez) -- el paquete inyectado nace siempre valid=true, con el header
// calculado aca.
template <int DATA_W, int VLEN>
inline NoC_Packet_C<DATA_W, VLEN> make_injected(const PE_VectorData<DATA_W, VLEN>& local_in,
                                                 int dest_row, int dest_col) {
    NoC_Packet_C<DATA_W, VLEN> p;
    p.valid = true;
    p.dest_row = ap_uint<4>(dest_row);
    p.dest_col = ap_uint<4>(dest_col);
    p.data = local_in;
    return p;
}

// Prioridad fija, en este orden: transito recto (desde el lado opuesto) >
// transito doblando desde el este > transito doblando desde el oeste >
// inyeccion local nueva. Los 2 candidatos de "doblando" son paquetes DISTINTOS
// (uno llegado por in_E, otro por in_W) que en principio podrian estar validos
// el mismo ciclo -- por eso se arbitran como 2 slots separados, nunca
// fusionados en uno solo antes de esta funcion.
template <int DATA_W, int VLEN>
inline NoC_Packet_C<DATA_W, VLEN> arbitrate(const NoC_Packet_C<DATA_W, VLEN>& straight,
                                             const NoC_Packet_C<DATA_W, VLEN>& turn_e,
                                             const NoC_Packet_C<DATA_W, VLEN>& turn_w,
                                             const NoC_Packet_C<DATA_W, VLEN>& local) {
    if (straight.valid) return straight;
    if (turn_e.valid) return turn_e;
    if (turn_w.valid) return turn_w;
    if (local.valid) return local;
    return NoC_Packet_C<DATA_W, VLEN>();
}

template <int DATA_W, int VLEN>
inline NoC_Packet_C<DATA_W, VLEN> pick(bool cond, const NoC_Packet_C<DATA_W, VLEN>& p) {
    return cond ? p : NoC_Packet_C<DATA_W, VLEN>();
}

} // namespace noc_router_c_detail

// Funcion de ruteo: 100% combinacional, sin estado (ver comentario de
// cabecera). Un unico "ciclo" de router.
template <int DATA_W, int VLEN>
inline void noc_router_route(int row, int col, bool rst, bool enable,
                              const NoC_Packet_C<DATA_W, VLEN>& in_N,
                              const NoC_Packet_C<DATA_W, VLEN>& in_S,
                              const NoC_Packet_C<DATA_W, VLEN>& in_E,
                              const NoC_Packet_C<DATA_W, VLEN>& in_W,
                              const PE_VectorData<DATA_W, VLEN>& in_L_N,
                              const PE_VectorData<DATA_W, VLEN>& in_L_S,
                              const PE_VectorData<DATA_W, VLEN>& in_L_E,
                              const PE_VectorData<DATA_W, VLEN>& in_L_W,
                              NoC_RouterOut_C<DATA_W, VLEN>& out)
{
#pragma HLS INLINE
    using namespace noc_router_c_detail;
    typedef NoC_Packet_C<DATA_W, VLEN>  Packet;
    typedef PE_VectorData<DATA_W, VLEN> Link;

    if (rst || !enable) {
        out = NoC_RouterOut_C<DATA_W, VLEN>();
        return;
    }

    NextHop hN = next_hop(in_N, row, col);
    NextHop hS = next_hop(in_S, row, col);
    NextHop hE = next_hop(in_E, row, col);
    NextHop hW = next_hop(in_W, row, col);

    // Entrega local: arrival-direction-preserving -- un paquete que llega por
    // in_X y ya esta en destino siempre sale por out_L_X (nunca por otro
    // puerto local), asi que nunca hay contienda aca. Se entrega solo el
    // payload: la celda adjunta lee un Link liso, igual que siempre.
    out.local_N = (hN == HOP_LOCAL) ? in_N.data : Link();
    out.local_S = (hS == HOP_LOCAL) ? in_S.data : Link();
    out.local_E = (hE == HOP_LOCAL) ? in_E.data : Link();
    out.local_W = (hW == HOP_LOCAL) ? in_W.data : Link();

    // Inyeccion local: siempre exactamente 1 salto hacia el vecino inmediato.
    Packet injN = make_injected(in_L_N, row - 1, col);
    Packet injS = make_injected(in_L_S, row + 1, col);
    Packet injE = make_injected(in_L_E, row,     col + 1);
    Packet injW = make_injected(in_L_W, row,     col - 1);

    // Puertos de malla: out_N/out_S pueden recibir transito recto desde el
    // lado opuesto (S->N, N->S) o transito doblando desde E/W; out_E/out_W
    // solo reciben transito recto desde el lado opuesto -- un paquete que ya
    // esta en fase de fila (llego por N o S) nunca vuelve a cambiar de columna
    // bajo XY routing.
    out.mesh_N = arbitrate(pick(hS == HOP_N, in_S), pick(hE == HOP_N, in_E), pick(hW == HOP_N, in_W), injN);
    out.mesh_S = arbitrate(pick(hN == HOP_S, in_N), pick(hE == HOP_S, in_E), pick(hW == HOP_S, in_W), injS);
    out.mesh_E = arbitrate(pick(hW == HOP_E, in_W), Packet(), Packet(), injE);
    out.mesh_W = arbitrate(pick(hE == HOP_W, in_E), Packet(), Packet(), injW);
}

#endif // NOC_ROUTER_C_H
