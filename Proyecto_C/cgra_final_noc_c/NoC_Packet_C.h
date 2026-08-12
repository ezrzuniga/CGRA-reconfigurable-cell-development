// NoC_Packet_C.h
// Transliteracion a C/C++ puro de cgra_final_noc/NoC_Packet.h: tipo de paquete
// (flit) de la fabrica de interconexion NoC, sobre ap_uint en vez de sc_uint y
// sin los overloads de operator<</sc_trace (no aplican fuera de SystemC).
//
// Que es un NoC (Network-on-Chip) y por que difiere de
// mesh_hls_c/CGRA_Mesh_Static_C.h: un NoC modela la comunicacion interna del
// chip como una red de PAQUETES con ROUTERS y ENLACES, en vez de cablear cada
// bloque a sus vecinos con wires dedicados. Cada paquete lleva su propio
// destino (header) junto con el dato (payload); cada router decide, ciclo a
// ciclo y de forma dinamica, hacia que puerto reenviar cada paquete segun ese
// header -- la decision de ruteo vive en el dato que viaja, no en una
// configuracion estatica precargada. Es la solucion estandar para sistemas
// complejos con muchos maestros/esclavos porque escala mejor que wires punto a
// punto: agregar un nodo nuevo cuesta un router mas, no una reconexion
// completa de la malla.
//
// Contraste con lo que ya existe en este repo (pe_hls_c/routing/
// Routing_Cell_HLS_C.h): esa celda YA es un switch-box, pero es
// CIRCUIT-SWITCHED -- el mux de cada salida esta fijado por un contexto
// preconfigurado (config_bank/active_ctx) cargado de antemano por el host; el
// dato mismo no lleva ninguna direccion. NoC_Router_C.h reusa exactamente esa
// misma forma de 8 puertos, pero reemplaza el mux estatico por una decision de
// ruteo POR PAQUETE (XY / dimension-order routing sobre dest_row/dest_col),
// que es la diferencia real entre "interconexion de malla directa" y
// "Network-on-Chip".
//
// NoC_Packet_C<DATA_W,VLEN> es el "flit" unico de esta red (paquetes de un
// solo flit -- el payload siempre cabe en un ciclo, igual que Link ya lo hace
// en el resto del repo): valid (si hay dato real este ciclo), dest_row/dest_col
// (coordenadas destino dentro de la malla ROWS x COLS) y data (el mismo
// PE_VectorData<DATA_W,VLEN> = Link que usa toda la CGRA). 4 bits alcanzan de
// sobra para direccionar mallas de hasta 16x16 sin cambiar el tipo.
//
// El operator== del original desaparece aca: existia unicamente porque
// sc_signal<T> lo usa para decidir si hubo un evento real en un delta-ciclo.
// En C no hay eventos -- packet_equal() (abajo) queda solo como utilidad de
// testbench, fuera del datapath.

#ifndef NOC_PACKET_C_H
#define NOC_PACKET_C_H

#include "../pe_hls_c/pe_isa_hls_c.h"

template <int DATA_W = 32, int VLEN = 4>
struct NoC_Packet_C {
    bool                        valid;
    ap_uint<4>                  dest_row;
    ap_uint<4>                  dest_col;
    PE_VectorData<DATA_W, VLEN> data;

    NoC_Packet_C() : valid(false), dest_row(0), dest_col(0), data() {}
};

// Dos paquetes invalidos son "iguales" sin comparar header/payload -- misma
// convencion que tenia el operator== del original.
template <int DATA_W, int VLEN>
inline bool packet_equal(const NoC_Packet_C<DATA_W, VLEN>& a, const NoC_Packet_C<DATA_W, VLEN>& b) {
    if (!a.valid && !b.valid) return true;
    if (a.valid != b.valid) return false;
    if (a.dest_row.to_uint() != b.dest_row.to_uint()) return false;
    if (a.dest_col.to_uint() != b.dest_col.to_uint()) return false;
    for (int i = 0; i < VLEN; i++) if (a.data[i].to_int() != b.data[i].to_int()) return false;
    return true;
}

#endif // NOC_PACKET_C_H
