// Routing_Cell_HLS_C__TB.cpp
// Testbench plano de Routing_Cell_HLS_C.h: programa el contexto 0 para
// cruzar E<-W y W<-E, y verifica el mux combinacional resultante.
#include <cstdio>
#include "Routing_Cell_HLS_C.h"

static const int DATA_W = 32, VLEN = 1;
typedef Routing_Cell_State<DATA_W, VLEN> State;

int main() {
    bool ok = true;

    State s;
    PE_VectorData<DATA_W, VLEN> zero;
    routing_cell_step(s, /*rst=*/true, /*enable=*/true, zero, zero, zero, zero);  // limpia config_bank

    routing_cell_program(s, 0, make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_FROM_E));

    PE_VectorData<DATA_W, VLEN> in_N, in_S, in_E, in_W;
    in_W[0] = 7;
    in_E[0] = 9;
    routing_cell_step(s, /*rst=*/false, /*enable=*/true, in_N, in_S, in_E, in_W);

    bool pass_e = (s.out_E[0].to_int() == 7);
    bool pass_w = (s.out_W[0].to_int() == 9);
    bool pass_n = (s.out_N[0].to_int() == 0);  // sel_N=RC_NONE
    printf("%s out_E = in_W  esperado=7 obtenido=%d\n", pass_e ? "PASS" : "FAIL", s.out_E[0].to_int());
    printf("%s out_W = in_E  esperado=9 obtenido=%d\n", pass_w ? "PASS" : "FAIL", s.out_W[0].to_int());
    printf("%s out_N = NONE  esperado=0 obtenido=%d\n", pass_n ? "PASS" : "FAIL", s.out_N[0].to_int());
    ok = pass_e && pass_w && pass_n;

    // enable=false debe forzar todas las salidas a 0.
    routing_cell_step(s, false, /*enable=*/false, in_N, in_S, in_E, in_W);
    bool pass_disabled = (s.out_E[0].to_int() == 0 && s.out_W[0].to_int() == 0);
    printf("%s todas las salidas en 0 con enable=false\n", pass_disabled ? "PASS" : "FAIL");
    ok = ok && pass_disabled;

    if (ok) printf("\nPASS: Routing_Cell_HLS_C cruza E<-W y W<-E segun el contexto programado.\n");
    return ok ? 0 : 1;
}
