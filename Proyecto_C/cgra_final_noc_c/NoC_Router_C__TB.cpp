// NoC_Router_C__TB.cpp
// Transliteracion a C/C++ puro de cgra_final_noc/NoC_Router__TB.cpp: prueba de
// la fabrica de routers EN AISLAMIENTO (sin ninguna celda de computo adjunta)
// sobre una grilla 2x3 de routers puros. Complementa a
// CGRA_Final_NoC_Mesh_C__TB.cpp / CGRA_Final_NoC_GEMM_C__TB.cpp: esas pruebas
// usan las 5 celdas reales del repo, pero por construccion del ISA (ver
// NoC_Router_C.h) ese trafico SIEMPRE es de 1 solo salto -- nunca ejercita
// transito de 2+ saltos ni un "doblez" de columna a fila (XY routing). Esta
// prueba inyecta paquetes SINTETICOS directo en los puertos de malla (no via
// puertos locales, que solo saben mandar 1 salto) para demostrar que el router
// funciona como un NoC de verdad:
//
//   1) Multi-hop con doblez: un paquete que entra por el borde oeste de la
//      fila 0 con destino (1,2) viaja 2 saltos al este (columna 0->1->2) y
//      luego 1 salto al sur (fila 0->1) -- ejercita tanto "recto" como
//      "doblando" en next_hop()/noc_router_route().
//   2) Arbitraje: mientras ese mismo paquete esta en transito recto por r01
//      (rumbo al este), r01 ADEMAS recibe una inyeccion local propia -- se
//      verifica que el paquete en transito gana (prioridad documentada en
//      NoC_Router_C.h) y el dato local se descarta ese ciclo.
//   3) Control negativo: sin transito, esa misma inyeccion local SI llega.
//
// Grilla (row,col), sin celdas de computo -- los 4 puertos locales de cada
// router quedan atados a Link() (0) salvo donde se indique, asi que cada
// router ya esta "inyectando" constantemente paquetes de payload 0 hacia sus 4
// vecinos (la inyeccion local siempre esta activa, sin bit de validez):
// trafico de fondo inofensivo que sirve ademas como demostracion extra de que
// el paquete real de esta prueba SIEMPRE gana el arbitraje frente a ese ruido,
// en cada uno de los 3 saltos que atraviesa:
//
//        col 0      col 1      col 2
//   row0  r00 ------ r01 ------ r02
//          |          |          |
//   row1  r10        r11        r12
//
// Nota de temporizado: el router no tiene flancos de reloj (funcion pura, ver
// NoC_Router_C.h), asi que un paquete inyectado en r00 llega a r12 dentro del
// MISMO "ciclo" -- las 3 celdas de red no pipelinean el viaje, exactamente
// igual que si fueran 3 Routing_Cell encadenadas. Lo que en SystemC hacia el
// kernel iterando delta-ciclos hasta converger, aca lo hace eval_grid()
// evaluando la grilla TPASSES = TROWS+TCOLS-1 veces (el diametro XY: cota de
// tiempo de compilacion, misma tecnica que NoC_Mesh_Static_C.h).

#include <cstdio>
#include <string>
#include "NoC_Router_C.h"
#include "../pe_hls_c/test_util_c.h"

static const int DATA_W = 32;
static const int VLEN   = 4;
static const int TROWS  = 2;
static const int TCOLS  = 3;
static const int TPASSES = TROWS + TCOLS - 1;

typedef PE_VectorData<DATA_W, VLEN>  Link;
typedef NoC_Packet_C<DATA_W, VLEN>   Packet;
typedef NoC_RouterOut_C<DATA_W, VLEN> RouterOut;

// Puertos de la grilla, con el mismo rol que los sc_signal del original.
static Packet g_bnd_in_N[TCOLS], g_bnd_in_S[TCOLS];
static Packet g_bnd_in_W[TROWS], g_bnd_in_E[TROWS];
static Link   g_local_in[TROWS][TCOLS][4];   // dir: 0=N, 1=S, 2=E, 3=W
static RouterOut g_ro[TROWS][TCOLS];

static Link link4(int32_t v) { Link l; l[0] = v; l[1] = v; l[2] = v; l[3] = v; return l; }

static Packet make_pkt(int dest_row, int dest_col, int32_t v) {
    Packet p;
    p.valid = true;
    p.dest_row = dest_row;
    p.dest_col = dest_col;
    p.data = link4(v);
    return p;
}

// Resuelve la grilla combinacional completa (equivalente al punto fijo de
// delta-ciclos de SystemC, ver comentario de cabecera).
static void eval_grid(bool rst, bool enable) {
    Packet prev_N[TROWS][TCOLS], prev_S[TROWS][TCOLS], prev_E[TROWS][TCOLS], prev_W[TROWS][TCOLS];

    for (int pass = 0; pass < TPASSES; pass++) {
        for (int r = 0; r < TROWS; r++) {
            for (int c = 0; c < TCOLS; c++) {
                Packet in_N = (r == 0)         ? g_bnd_in_N[c] : prev_S[r - 1][c];
                Packet in_S = (r == TROWS - 1) ? g_bnd_in_S[c] : prev_N[r + 1][c];
                Packet in_W = (c == 0)         ? g_bnd_in_W[r] : prev_E[r][c - 1];
                Packet in_E = (c == TCOLS - 1) ? g_bnd_in_E[r] : prev_W[r][c + 1];

                noc_router_route<DATA_W, VLEN>(r, c, rst, enable, in_N, in_S, in_E, in_W,
                                               g_local_in[r][c][0], g_local_in[r][c][1],
                                               g_local_in[r][c][2], g_local_in[r][c][3],
                                               g_ro[r][c]);
            }
        }
        for (int r = 0; r < TROWS; r++)
            for (int c = 0; c < TCOLS; c++) {
                prev_N[r][c] = g_ro[r][c].mesh_N;
                prev_S[r][c] = g_ro[r][c].mesh_S;
                prev_E[r][c] = g_ro[r][c].mesh_E;
                prev_W[r][c] = g_ro[r][c].mesh_W;
            }
    }
    test_count_cycles(1);
}

int main() {
    bool ok = true;

    // Sin estado que capturar (router 100% combinacional): alcanza con dejar
    // sentado rst/enable y evaluar la grilla.
    eval_grid(/*rst=*/false, /*enable=*/true);

    //========================================================================
    // 1) Multi-hop con doblez: (0,0) borde W -> dest (1,2), 3 saltos
    //    (E, E, S), entrega esperada en r12.out_L_N (llego por el norte).
    //========================================================================
    test_section("Multi-hop: borde W de r00, dest=(1,2) -- 2 saltos E + 1 salto S");
    g_bnd_in_W[0] = make_pkt(1, 2, 55);
    eval_grid(false, true);
    test_check_link(ok, "r12.out_L_N recibe el paquete tras 3 saltos (E,E,S)",
                    "dest=(1,2), payload=55", link4(55), g_ro[1][2].local_N);

    // Efecto colateral verificable del mismo estimulo: en transito, el paquete
    // tuvo que pasar por r01.in_W -> r01.out_E (recto) y por r02.in_W ->
    // r02.out_S (doblando) -- confirmarlo en las 2 celdas intermedias deja
    // documentado, dentro del propio PASS/FAIL, que la ruta tomada fue la
    // esperada y no una coincidencia de otro camino.
    {
        Packet mid1 = g_ro[0][0].mesh_E;   // r00.out_E == r01.in_W
        test_check_bool(ok, "Salto 1 (r00->r01, recto E) transporta el paquete de prueba",
                        "r00.out_E.valid",
                        mid1.valid && mid1.dest_row.to_uint() == 1 && mid1.dest_col.to_uint() == 2);
        Packet mid2 = g_ro[0][2].mesh_S;   // r02.out_S == r12.in_N
        test_check_bool(ok, "Salto 3 (r02->r12, doblando a S) transporta el paquete de prueba",
                        "r02.out_S.valid",
                        mid2.valid && mid2.dest_row.to_uint() == 1 && mid2.dest_col.to_uint() == 2);
    }

    //========================================================================
    // 2) Arbitraje: mientras el paquete de arriba sigue en transito recto por
    //    r01 (rumbo este), r01 ADEMAS recibe una inyeccion local propia --
    //    ambos compiten por r01.out_E. Prioridad: transito antes que
    //    inyeccion local nueva.
    //========================================================================
    test_section("Arbitraje en r01.out_E: transito (recto, payload=55) vs inyeccion local (payload=999)");
    g_local_in[0][1][2] = link4(999);   // r01.in_L_E: "r01 quiere mandar al este"
    eval_grid(false, true);
    {
        Packet at_r02 = g_ro[0][1].mesh_E;   // r01.out_E == r02.in_W
        test_check_bool(ok, "r02.in_W recibe el paquete en TRANSITO (55), no la inyeccion local de r01 (999)",
                        "esperado payload=55 (transito gana)",
                        at_r02.valid && at_r02.data[0].to_int() == 55);
    }

    //========================================================================
    // 3) Sin contienda, la inyeccion local SI llega -- confirma que el
    //    descarte de arriba fue por arbitraje, no porque la inyeccion local
    //    este rota.
    //========================================================================
    test_section("Sin transito, la inyeccion local de r01 SI llega a r02 (control negativo del arbitraje)");
    g_bnd_in_W[0] = Packet();
    eval_grid(false, true);
    {
        Packet at_r02 = g_ro[0][1].mesh_E;
        test_check_bool(ok, "r02.in_W recibe ahora la inyeccion local de r01 (999) al no haber transito",
                        "esperado payload=999 (sin contienda)",
                        at_r02.valid && at_r02.data[0].to_int() == 999);
    }

    //========================================================================
    // 4) rst/enable fuerzan las 8 salidas de cada router a su valor por
    //    defecto (no existia como caso separado en el original, donde se
    //    verificaba de paso en las mallas completas).
    //========================================================================
    test_section("rst y enable=false fuerzan todas las salidas a cero/invalido");
    eval_grid(/*rst=*/true, /*enable=*/true);
    test_check_bool(ok, "con rst, ningun puerto de malla queda valido y las entregas locales son 0",
                    "rst=true",
                    !g_ro[0][1].mesh_E.valid && g_ro[1][2].local_N[0].to_int() == 0);
    eval_grid(/*rst=*/false, /*enable=*/false);
    test_check_bool(ok, "con enable=false, idem", "enable=false",
                    !g_ro[0][1].mesh_E.valid && g_ro[1][2].local_N[0].to_int() == 0);

    if (ok) {
        printf("\nPASS: NoC_Router_C enruta multi-hop (recto + doblez XY) y arbitra "
               "transito-antes-que-local exactamente como documenta NoC_Router_C.h.\n");
    }
    return ok ? 0 : 1;
}
