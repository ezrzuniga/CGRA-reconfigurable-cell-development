// NoC_Packet.h
// Tipo de paquete (flit) de la fabrica de interconexion NoC de cgra_final_noc/.
//
// Que es un NoC (Network-on-Chip) y por que difiere de mesh_hls/CGRA_Mesh_Static.h:
// un NoC modela la comunicacion interna del chip como una red de PAQUETES con
// ROUTERS y ENLACES, en vez de cablear cada bloque a sus vecinos con wires
// dedicados. Cada paquete lleva su propio destino (header) junto con el dato
// (payload); cada router decide, ciclo a ciclo y de forma dinamica, hacia que
// puerto reenviar cada paquete segun ese header -- la decision de ruteo vive
// en el dato que viaja, no en una configuracion estatica precargada. Es la
// solucion estandar para sistemas complejos con muchos maestros/esclavos
// (CPUs, DMAs, aceleradores, memoria) porque escala mejor que wires punto a
// punto dedicados: agregar un nodo nuevo cuesta un router mas, no una
// reconexion completa de la malla.
//
// Contraste con lo que ya existe en este repo (pe_hls/routing/Routing_Cell.h):
// esa celda YA es un switch-box de 8 puertos (4 de malla + 4 locales), pero es
// CIRCUIT-SWITCHED -- el mux de cada salida esta fijado por un contexto
// preconfigurado (config_bank/ctx_sel) cargado de antemano por el host; el
// dato mismo no lleva ninguna direccion. NoC_Router.h (mismo directorio)
// reusa exactamente esa misma forma de 8 puertos, pero reemplaza el mux
// estatico por una decision de ruteo POR PAQUETE (XY / dimension-order
// routing sobre dest_row/dest_col), que es la diferencia real entre
// "interconexion de malla directa" y "Network-on-Chip".
//
// NoC_Packet<DATA_W,VLEN> es el "flit" unico de esta red (paquetes de un solo
// flit -- el payload siempre cabe en un ciclo, igual que Link ya lo hace en
// el resto del repo): valid (si hay dato real este ciclo), dest_row/dest_col
// (coordenadas destino dentro de la malla ROWS x COLS) y data (el mismo
// PE_VectorData<DATA_W,VLEN> = Link que usa toda la CGRA). 4 bits alcanzan de
// sobra para direccionar mallas de hasta 16x16 sin cambiar el tipo.

#ifndef NOC_PACKET_H
#define NOC_PACKET_H

#include <systemc.h>
#include "../pe_hls/pe_isa.h"

template <int DATA_W = 32, int VLEN = 4>
struct NoC_Packet {
    bool valid;
    sc_uint<4> dest_row;
    sc_uint<4> dest_col;
    PE_VectorData<DATA_W, VLEN> data;

    NoC_Packet() : valid(false), dest_row(0), dest_col(0), data() {}

    inline bool operator==(const NoC_Packet<DATA_W, VLEN>& o) const {
        // Dos paquetes invalidos son "iguales" sin comparar header/payload
        // (sc_signal<T> usa operator== para decidir si hubo un evento real;
        // dos "nada este ciclo" nunca deben disparar un evento).
        if (!valid && !o.valid) return true;
        return valid == o.valid && dest_row == o.dest_row &&
               dest_col == o.dest_col && data == o.data;
    }
};

template <int DATA_W, int VLEN>
inline ostream& operator<<(ostream& os, const NoC_Packet<DATA_W, VLEN>& p) {
    os << "{valid=" << p.valid << ", dest=(" << p.dest_row << "," << p.dest_col
       << "), data=" << p.data << "}";
    return os;
}

template <int DATA_W, int VLEN>
inline void sc_trace(sc_core::sc_trace_file* tf, const NoC_Packet<DATA_W, VLEN>& p, const std::string& name) {
    sc_trace(tf, p.valid,    name + ".valid");
    sc_trace(tf, p.dest_row, name + ".dest_row");
    sc_trace(tf, p.dest_col, name + ".dest_col");
    sc_trace(tf, p.data,     name + ".data");
}

#endif // NOC_PACKET_H
