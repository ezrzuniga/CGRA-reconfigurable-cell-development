// NoC_Router__TB.cpp
// Prueba de la fabrica de routers EN AISLAMIENTO (sin ninguna celda de
// computo adjunta) sobre una grilla 2x3 de NoC_Router puros. Complementa a
// CGRA_Final_NoC_Mesh__TB.cpp/CGRA_Final_NoC_GEMM__TB.cpp: esas pruebas usan
// las 5 celdas reales del repo, pero por construccion del ISA (ver
// NoC_Router.h) ese trafico SIEMPRE es de 1 solo salto -- nunca ejercita
// transito de 2+ saltos ni un "doblez" de columna a fila (XY routing). Esta
// prueba inyecta paquetes SINTETICOS directo en los puertos de malla (no via
// puertos locales, que solo saben mandar 1 salto -- ver NoC_Router.h) para
// demostrar que el router funciona como un NoC de verdad:
//
//   1) Multi-hop con doblez: un paquete que entra por el borde oeste de la
//      fila 0 con destino (1,2) viaja 2 saltos al este (columna 0->1->2) y
//      luego 1 salto al sur (fila 0->1) -- ejercita tanto "recto" como
//      "doblando" en next_hop()/route().
//   2) Arbitraje: mientras ese mismo paquete esta en transito recto por
//      r01 (rumbo al este), r01 ADEMAS recibe una inyeccion local propia
//      (su celda adjunta "querria" mandar algo al este ese mismo ciclo) --
//      se verifica que el paquete en transito gana (prioridad documentada en
//      NoC_Router.h) y el dato local se descarta ese ciclo.
//
// Grilla (row,col), sin celdas de computo -- los 4 puertos locales de cada
// router quedan atados a Link() (0) salvo donde se indica lo contrario, asi
// que cada router de por si ya esta "inyectando" constantemente paquetes de
// carga util 0 hacia sus 4 vecinos (ver NoC_Router.h: la inyeccion local
// siempre esta activa, sin bit de validez) -- trafico de fondo inofensivo
// que sirve ademas como demostracion extra de que el paquete real de esta
// prueba SIEMPRE gana el arbitraje frente a ese ruido, en cada uno de los 3
// saltos que atraviesa:
//
//        col 0      col 1      col 2
//   row0  r00 ------ r01 ------ r02
//          |          |          |
//   row1  r10        r11        r12
//
// Nota de temporizado (ver NoC_Router.h, ultimo parrafo): al no haber ningun
// flanco de reloj dentro del router (100% combinacional, igual que
// Routing_Cell), un paquete inyectado en r00 llega a r12 dentro del MISMO
// ciclo de reloj -- las 3 celdas de red de este ejemplo no pipelinean el
// viaje, exactamente igual que si fueran 3 Routing_Cell encadenadas.

#include <systemc.h>
#include "NoC_Router.h"
#include "../pe_hls/test_util.h"

typedef NoC_Router<32, 4> Router;
typedef Router::Link   Link;
typedef Router::Packet Packet;

static const int TROWS = 2;
static const int TCOLS = 3;

static Packet make_pkt(int dest_row, int dest_col, int32_t v) {
    Packet p;
    p.valid = true;
    p.dest_row = dest_row;
    p.dest_col = dest_col;
    p.data = Link({v, v, v, v});
    return p;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;

    Router r00("r00", 0, 0), r01("r01", 0, 1), r02("r02", 0, 2);
    Router r10("r10", 1, 0), r11("r11", 1, 1), r12("r12", 1, 2);
    Router* grid[TROWS][TCOLS] = {{&r00, &r01, &r02}, {&r10, &r11, &r12}};

    // Enlaces de malla internos (horizontales E-W dentro de cada fila,
    // verticales N-S entre las 2 filas) -- mismo indexado por posicion que
    // NoC_Mesh_Static::wire_one, escrito a mano por ser una grilla fija chica.
    sc_signal<Packet> h_e[TROWS][TCOLS - 1], h_w[TROWS][TCOLS - 1];  // out_E->in_W, out_W->in_E
    sc_signal<Packet> v_s[TROWS - 1][TCOLS], v_n[TROWS - 1][TCOLS];  // out_S->in_N, out_N->in_S

    // Puertos de borde (sin vecino real) -- bnd_in_W[0] es el punto de
    // inyeccion del paquete de prueba; el resto solo necesita quedar
    // atado a algo valido (nunca escritos => Packet() por defecto, invalido).
    sc_signal<Packet> bnd_in_N[TCOLS], bnd_out_N[TCOLS];
    sc_signal<Packet> bnd_in_S[TCOLS], bnd_out_S[TCOLS];
    sc_signal<Packet> bnd_in_W[TROWS], bnd_out_W[TROWS];
    sc_signal<Packet> bnd_in_E[TROWS], bnd_out_E[TROWS];

    // Puertos locales: Link liso (sin celda adjunta real) -- un signal por
    // in_L_X/out_L_X de cada router. local_in[r][c][dir] hacia el router,
    // local_out[r][c][dir] desde el router (dir: 0=N,1=S,2=E,3=W).
    sc_signal<Link> local_in[TROWS][TCOLS][4], local_out[TROWS][TCOLS][4];

    for (int r = 0; r < TROWS; r++) {
        for (int c = 0; c < TCOLS; c++) {
            Router& rt = *grid[r][c];
            rt.clk(clk); rt.rst(rst); rt.enable(enable);

            rt.in_L_N(local_in[r][c][0]); rt.out_L_N(local_out[r][c][0]);
            rt.in_L_S(local_in[r][c][1]); rt.out_L_S(local_out[r][c][1]);
            rt.in_L_E(local_in[r][c][2]); rt.out_L_E(local_out[r][c][2]);
            rt.in_L_W(local_in[r][c][3]); rt.out_L_W(local_out[r][c][3]);

            if (r == 0) { rt.in_N(bnd_in_N[c]); rt.out_N(bnd_out_N[c]); }
            else        { rt.in_N(v_s[r - 1][c]); rt.out_N(v_n[r - 1][c]); }

            if (r == TROWS - 1) { rt.in_S(bnd_in_S[c]); rt.out_S(bnd_out_S[c]); }
            else                { rt.in_S(v_n[r][c]); rt.out_S(v_s[r][c]); }

            if (c == 0) { rt.in_W(bnd_in_W[r]); rt.out_W(bnd_out_W[r]); }
            else        { rt.in_W(h_e[r][c - 1]); rt.out_W(h_w[r][c - 1]); }

            if (c == TCOLS - 1) { rt.in_E(bnd_in_E[r]); rt.out_E(bnd_out_E[r]); }
            else                { rt.in_E(h_w[r][c]); rt.out_E(h_e[r][c]); }
        }
    }

    rst.write(false);
    enable.write(true);
    // Sin flancos de reloj que capturar estado (router 100% combinacional,
    // ver comentario de cabecera) -- alcanza con dejar sentado rst/enable.
    advance_cycles(1);

    bool ok = true;

    //========================================================================
    // 1) Multi-hop con doblez: (0,0) borde W -> dest (1,2), 3 saltos
    //    (E, E, S), entrega esperada en r12.out_L_N (llego por el norte).
    //========================================================================
    test_section("Multi-hop: borde W de r00, dest=(1,2) -- 2 saltos E + 1 salto S");
    bnd_in_W[0].write(make_pkt(1, 2, 55));
    advance_cycles(1);
    {
        Link expected({55, 55, 55, 55});
        Link got = local_out[1][2][0].read();  // r12.out_L_N
        test_check(ok, "r12.out_L_N recibe el paquete tras 3 saltos (E,E,S)", "dest=(1,2), payload=55",
                   expected, got);
    }
    // Efecto colateral verificable del mismo estimulo: en transito, el
    // paquete tuvo que pasar por r01.in_W -> r01.out_E (recto) y por
    // r02.in_W -> r02.out_S (doblando) -- confirmarlo en las 2 celdas
    // intermedias deja documentado, dentro del propio PASS/FAIL, que la
    // ruta tomada fue la esperada y no una coincidencia de otro camino.
    {
        Packet mid1 = h_e[0][0].read();  // r00.out_E == r01.in_W
        test_check_bool(ok, "Salto 1 (r00->r01, recto E) transporta el paquete de prueba", "h_e[0][0].valid",
                         mid1.valid && mid1.dest_row == 1 && mid1.dest_col == 2);
        Packet mid2 = v_s[0][2].read();  // r02.out_S == r12.in_N
        test_check_bool(ok, "Salto 3 (r02->r12, doblando a S) transporta el paquete de prueba", "v_s[0][2].valid",
                         mid2.valid && mid2.dest_row == 1 && mid2.dest_col == 2);
    }

    //========================================================================
    // 2) Arbitraje: mientras el paquete de arriba sigue en transito recto
    //    por r01 (rumbo este), r01 ADEMAS recibe una inyeccion local propia
    //    -- ambos compiten por r01.out_E. Prioridad documentada en
    //    NoC_Router.h: transito antes que inyeccion local nueva.
    //========================================================================
    test_section("Arbitraje en r01.out_E: transito (recto, payload=55) vs inyeccion local (payload=999)");
    local_in[0][1][2].write(Link({999, 999, 999, 999}));  // r01.in_L_E: "r01 quiere mandar al este"
    advance_cycles(1);
    {
        Packet at_r02 = h_e[0][1].read();  // r01.out_E == r02.in_W
        test_check_bool(ok, "r02.in_W recibe el paquete en TRANSITO (55), no la inyeccion local de r01 (999)",
                         "esperado payload=55 (transito gana)",
                         at_r02.valid && at_r02.data[0] == 55);
    }

    //========================================================================
    // 3) Sin contienda, la inyeccion local SI llega -- confirma que el
    //    descarte de arriba fue por arbitraje, no porque la inyeccion local
    //    este rota. Se retira el estimulo de borde (bnd_in_W[0]) para que
    //    r01 ya no tenga trafico en transito, y se repite la misma
    //    inyeccion local hacia r01.in_L_E.
    //========================================================================
    test_section("Sin transito, la inyeccion local de r01 SI llega a r02 (control negativo del arbitraje)");
    bnd_in_W[0].write(Packet());
    advance_cycles(1);
    {
        Packet at_r02 = h_e[0][1].read();  // r01.out_E == r02.in_W
        test_check_bool(ok, "r02.in_W recibe ahora la inyeccion local de r01 (999) al no haber transito",
                         "esperado payload=999 (sin contienda)",
                         at_r02.valid && at_r02.data[0] == 999);
    }

    if (ok) {
        cout << "\nPASS: NoC_Router enruta multi-hop (recto + doblez XY) y arbitra "
             << "transito-antes-que-local exactamente como documenta NoC_Router.h." << endl;
    }
    return ok ? 0 : 1;
}
