// NoC_Router.h
// Router de la fabrica NoC: mismo contrato de 8 puertos de dato que
// pe_hls/routing/Routing_Cell.h (4 "de malla" N/S/E/W hacia routers vecinos +
// 4 "locales" L_N/L_S/L_E/L_W hacia la celda de computo adjunta, espejo 1:1
// de PE_Base: celda.out_X <-> router.in_L_X, router.out_L_X <-> celda.in_X),
// pero en vez de un mux fijado por configuracion estatica (RC_Config), la
// salida se decide EN CADA CICLO a partir del header (dest_row,dest_col) que
// cada NoC_Packet lleva encima -- ver NoC_Packet.h para la comparacion
// completa circuit-switched vs packet-switched.
//
// Algoritmo de ruteo: XY (dimension-order), columna primero, fila despues --
// el mismo algoritmo textbook que usan la mayoria de NoCs de malla 2D porque
// es libre de deadlock sin necesidad de virtual channels. Un router en
// (row_,col_) que ve un paquete con destino (dr,dc):
//   1. si dc != col_: el paquete tiene que seguir moviéndose en columna
//      (out_E si dc > col_, out_W si dc < col_).
//   2. si dc == col_ pero dr != row_: recien ahora se permite "doblar" a
//      fila (out_S si dr > row_, out_W si dr < row_ -- fila crece hacia el
//      sur, igual que las filas de CGRA_Mesh_Static).
//   3. si dc == col_ y dr == row_: el paquete llego -- se entrega al puerto
//      local que mira hacia la direccion por la que llego (in_N -> out_L_N,
//      etc.), nunca a un puerto local distinto.
//
// Inyeccion local (in_L_N/S/E/W, tipo Link -- NO Packet: espejo exacto de
// PE_Base/Routing_Cell, la celda adjunta no sabe nada de paquetes, solo
// escribe su out_N/S/E/W de siempre): por construccion del ISA de este repo
// (pe_hls/pe_isa.h, SRC_/DST_NORTH|SOUTH|EAST|WEST) una celda SOLO puede
// dirigirse a su vecino INMEDIATO -- nunca hay una direccion de "2 saltos" en
// una instruccion. Por eso el router arma el header de un paquete inyectado
// localmente con su PROPIA posicion +-1 en el eje correspondiente (in_L_N ->
// dest=(row_-1,col_), etc.): siempre exactamente 1 salto, calculado aca, sin
// que la celda adjunta sepa nada de direcciones. Como Link no tiene bit de
// validez (la celda adjunta siempre esta manejando algo en sus 4 puertos,
// igual que en CGRA_Mesh_Static), el paquete inyectado se marca valid=true
// incondicionalmente cada ciclo -- exactamente la misma convencion "wire
// siempre manejado" que ya usa el resto del repo. Si el salto calculado cae
// fuera de la malla (borde), el valor de dest_row/dest_col resultante nunca
// se vuelve a leer -- el paquete sale directo por el puerto de malla de este
// mismo router hacia el borde externo (ver NoC_Mesh_Static.h,
// bridge_boundary_out), donde el header se descarta. Consecuencia importante
// y verificada en NoC_Router__TB.cpp: con el trafico que genera una celda de
// computo real (siempre 1 salto), un paquete SIEMPRE se entrega en el primer
// router al que llega -- nunca hay "transito" (2+ saltos) mas que en pruebas
// sinteticas que inyectan un header de destino lejano a mano.
//
// Arbitraje: solo puede haber contienda real en los 4 puertos de malla
// (out_N/S/E/W) --la entrega local (out_L_X) nunca compite, ver arriba-- y
// solo entre: la inyeccion local que apunta a esa direccion, el trafico recto
// que sigue de largo, y el trafico que recien esta "doblando" desde el otro
// eje. Prioridad fija: trafico en transito (recto y doblando) antes que
// inyeccion local nueva -- evita que un paquete que ya viaja hace 1+ saltos
// quede indefinidamente bloqueado por inyecciones frescas en cada router que
// atraviesa. Sin buffers ni credit-based flow control: el candidato que
// pierde el arbitraje se descarta ese ciclo (out_*.write(Packet()), es decir
// valid=false) -- limitacion conocida, documentada aca en vez de escondida;
// trabajo futuro razonable es agregar FIFOs de entrada + backpressure. Con el
// trafico real de este repo (siempre 0 o 1 salto, ver parrafo anterior) esta
// contienda NUNCA ocurre -- queda demostrado en NoC_Router__TB.cpp con un
// escenario que SI la fuerza a proposito.
//
// Igual que Routing_Cell (project.md: "la celda de enrutamiento, puramente
// combinacional"), este router no tiene estado propio ni flancos de reloj:
// toda la logica de route() es un SC_METHOD combinacional. Esto preserva la
// disciplina de temporizado del resto del repo -- un paquete inyectado en un
// router llega a su vecino exactamente en el mismo flanco de clk en que, en
// la malla de wires directos (CGRA_Mesh_Static), ese mismo dato hubiera
// llegado -- es decir, CGRA_Final_NoC_Mesh (cgra_final_noc/CGRA_Final_NoC_Mesh.h)
// es cycle-accurate identica a CGRA_Final_Mesh para el mismo estimulo, solo
// que el dato viaja empaquetado con header en vez de por un wire fijo. clk/rst
// se mantienen en la interfaz por uniformidad con el resto de celdas del
// repo (contrato documentado en mesh_hls/CGRA_Mesh_Static.h) aunque no haya
// flanco que capturar; rst, igual que enable=false, fuerza las 8 salidas a
// Packet() (valid=false) por seguridad/determinismo.

#ifndef NOC_ROUTER_H
#define NOC_ROUTER_H

#include <systemc.h>
#include "NoC_Packet.h"

template <int DATA_W = 32, int VLEN = 4>
class NoC_Router : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef NoC_Packet<DATA_W, VLEN> Packet;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    // Malla (routers vecinos) -- lleva el header, tipo Packet.
    sc_in<Packet>  in_N, in_S, in_E, in_W;
    sc_out<Packet> out_N, out_S, out_E, out_W;

    // Local (celda de computo adjunta) -- tipo Link liso, espejo 1:1 de
    // PE_Base/Routing_Cell::in_L_X/out_L_X: la celda no sabe nada de
    // paquetes, el router arma/quita el header aca mismo (ver route()).
    sc_in<Link>  in_L_N, in_L_S, in_L_E, in_L_W;
    sc_out<Link> out_L_N, out_L_S, out_L_E, out_L_W;

    SC_HAS_PROCESS(NoC_Router);

    NoC_Router(sc_core::sc_module_name name, int row, int col)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          in_L_N("in_L_N"), in_L_S("in_L_S"), in_L_E("in_L_E"), in_L_W("in_L_W"),
          out_L_N("out_L_N"), out_L_S("out_L_S"), out_L_E("out_L_E"), out_L_W("out_L_W"),
          row_(row), col_(col)
    {
        SC_METHOD(route);
        sensitive << rst << enable
                  << in_N << in_S << in_E << in_W
                  << in_L_N << in_L_S << in_L_E << in_L_W;
    }

    void trace(sc_core::sc_trace_file* tf) const {
        sc_trace(tf, out_N, std::string(name()) + ".out_N");
        sc_trace(tf, out_S, std::string(name()) + ".out_S");
        sc_trace(tf, out_E, std::string(name()) + ".out_E");
        sc_trace(tf, out_W, std::string(name()) + ".out_W");
        sc_trace(tf, out_L_N, std::string(name()) + ".out_L_N");
        sc_trace(tf, out_L_S, std::string(name()) + ".out_L_S");
        sc_trace(tf, out_L_E, std::string(name()) + ".out_L_E");
        sc_trace(tf, out_L_W, std::string(name()) + ".out_L_W");
    }

private:
    int row_, col_;

    enum NextHop { HOP_LOCAL = 0, HOP_N, HOP_S, HOP_E, HOP_W };

    // XY routing: columna primero, fila despues (ver comentario de cabecera).
    NextHop next_hop(const Packet& p) const {
        if (!p.valid) return HOP_LOCAL;  // paquete invalido: nunca compite por nada
        if ((int)p.dest_col.to_int() > col_) return HOP_E;
        if ((int)p.dest_col.to_int() < col_) return HOP_W;
        if ((int)p.dest_row.to_int() > row_) return HOP_S;
        if ((int)p.dest_row.to_int() < row_) return HOP_N;
        return HOP_LOCAL;
    }

    // La celda adjunta siempre esta manejando algo en su out_X (sin bit de
    // validez, igual que en CGRA_Mesh_Static) -- el paquete inyectado nace
    // siempre valid=true, con el header calculado aca (ver comentario de
    // cabecera).
    Packet make_injected(const Link& local_in, int dest_row, int dest_col) const {
        Packet p;
        p.valid = true;
        p.dest_row = sc_uint<4>(dest_row);
        p.dest_col = sc_uint<4>(dest_col);
        p.data = local_in;
        return p;
    }

    // Prioridad fija, en este orden: transito recto (desde el lado opuesto) >
    // transito doblando desde el este > transito doblando desde el oeste >
    // inyeccion local nueva (ver comentario de cabecera, seccion Arbitraje).
    // Los 2 candidatos de "doblando" son paquetes DISTINTOS (uno llegado por
    // in_E, otro por in_W) que en principio podrian estar validos el mismo
    // ciclo -- por eso se arbitran como 2 slots separados, nunca fusionados
    // en uno solo antes de esta funcion (fusionarlos silenciaria la perdida
    // del que no gana el desempate).
    static Packet arbitrate(const Packet& straight, const Packet& turn_e,
                             const Packet& turn_w, const Packet& local) {
        if (straight.valid) return straight;
        if (turn_e.valid) return turn_e;
        if (turn_w.valid) return turn_w;
        if (local.valid) return local;
        return Packet();
    }

    void route() {
        if (rst.read() || !enable.read()) {
            out_N.write(Packet());   out_S.write(Packet());
            out_E.write(Packet());   out_W.write(Packet());
            out_L_N.write(Link());   out_L_S.write(Link());
            out_L_E.write(Link());   out_L_W.write(Link());
            return;
        }

        Packet pN = in_N.read(), pS = in_S.read(), pE = in_E.read(), pW = in_W.read();

        NextHop hN = next_hop(pN), hS = next_hop(pS), hE = next_hop(pE), hW = next_hop(pW);

        // Entrega local: arrival-direction-preserving -- un paquete que llega
        // por in_X y ya esta en destino siempre sale por out_L_X (nunca por
        // otro puerto local), asi que nunca hay contienda aca. Se entrega
        // solo el payload (.data): la celda adjunta lee un Link liso, igual
        // que siempre.
        out_L_N.write(hN == HOP_LOCAL ? pN.data : Link());
        out_L_S.write(hS == HOP_LOCAL ? pS.data : Link());
        out_L_E.write(hE == HOP_LOCAL ? pE.data : Link());
        out_L_W.write(hW == HOP_LOCAL ? pW.data : Link());

        // Inyeccion local: siempre exactamente 1 salto hacia el vecino
        // inmediato en esa direccion (ver comentario de cabecera).
        Packet injN = make_injected(in_L_N.read(), row_ - 1, col_);
        Packet injS = make_injected(in_L_S.read(), row_ + 1, col_);
        Packet injE = make_injected(in_L_E.read(), row_,     col_ + 1);
        Packet injW = make_injected(in_L_W.read(), row_,     col_ - 1);

        // Puertos de malla: out_N/out_S solo pueden recibir transito recto
        // desde el lado opuesto (S->N, N->S) o transito doblando desde E/W
        // (ver derivacion en el comentario de cabecera); out_E/out_W solo
        // reciben transito recto desde el lado opuesto -- un paquete que ya
        // esta en fase de fila (llego por N o S) nunca vuelve a cambiar de
        // columna bajo XY routing.
        out_N.write(arbitrate(hS == HOP_N ? pS : Packet(),
                               hE == HOP_N ? pE : Packet(),
                               hW == HOP_N ? pW : Packet(),
                               injN));
        out_S.write(arbitrate(hN == HOP_S ? pN : Packet(),
                               hE == HOP_S ? pE : Packet(),
                               hW == HOP_S ? pW : Packet(),
                               injS));
        out_E.write(arbitrate(hW == HOP_E ? pW : Packet(), Packet(), Packet(), injE));
        out_W.write(arbitrate(hE == HOP_W ? pE : Packet(), Packet(), Packet(), injW));
    }
};

#endif // NOC_ROUTER_H
