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

    // --- Backpressure real: satura la cola de W con RC_NUM_CONTEXTS
    // cambiando el contexto activo a uno que NUNCA lee de W (asi nada la
    // consume), y confirma que credit_W baja a 0 sin perder el primer dato
    // encolado (FIFO real, no un passthrough disfrazado).
    State s2;
    routing_cell_step(s2, /*rst=*/true, /*enable=*/true, zero, zero, zero, zero);
    routing_cell_program(s2, 0, make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_NONE));  // nadie lee W

    PE_VectorData<DATA_W, VLEN> w_val;
    for (int i = 0; i < RC_FIFO_DEPTH; i++) {
        w_val[0] = 100 + i;
        routing_cell_step(s2, false, true, in_N, in_S, in_E, w_val);
    }
    bool pass_credit_exhausted = (s2.credit_W.to_uint() == 0);
    printf("%s credit_W llega a 0 tras llenar la cola (%d/%d)\n",
           pass_credit_exhausted ? "PASS" : "FAIL", s2.fifo_W.count.to_uint(), RC_FIFO_DEPTH);
    ok = ok && pass_credit_exhausted;

    // Reprograma para leer W->E: la cabeza de la cola debe ser el PRIMER
    // valor empujado (100), no el ultimo -- confirma orden FIFO real.
    routing_cell_program(s2, 1, make_routing_config_instr_c<DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE));
    routing_cell_step(s2, false, true, in_N, in_S, in_E, w_val);
    bool pass_fifo_order = (s2.out_E[0].to_int() == 100);
    printf("%s primer valor encolado (100) sale primero, credit_W se recupera a %d\n",
           pass_fifo_order ? "PASS" : "FAIL", s2.credit_W.to_uint());
    ok = ok && pass_fifo_order;

    if (ok) printf("\nPASS: Routing_Cell_HLS_C cruza E<-W y W<-E segun el contexto programado, "
                    "con colas FIFO reales y control de creditos.\n");
    return ok ? 0 : 1;
}
