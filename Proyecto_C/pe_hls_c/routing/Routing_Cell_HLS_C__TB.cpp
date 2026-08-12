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

    //=======================================================================
    // Switch-box completo de 8 puertos: los 4 puertos locales hacia la PE
    // co-ubicada (in_L_X <- PE.out_X, out_L_X -> PE.in_X). Ninguna malla de
    // este repo los cablea todavia, pero son parte del contrato de la celda
    // -- este caso es el unico que los ejercita.
    //
    // Contexto 1: la PE inyecta por su puerto oeste local y eso sale a la
    // malla por el norte (sel_N = RC_FROM_LW); en sentido inverso, lo que
    // entra de la malla por el sur se le entrega a la PE por su puerto este
    // local (sel_LE = RC_FROM_S). Es el patron tipico "PE adjunta usa el
    // switch-box como su puerta a la malla".
    //=======================================================================
    routing_cell_program(s, /*slot=*/1,
                         make_routing_config_instr_c<DATA_W>(
                             /*sel_N =*/RC_FROM_LW, /*sel_S =*/RC_NONE,
                             /*sel_E =*/RC_NONE,    /*sel_W =*/RC_NONE,
                             /*sel_LN=*/RC_NONE,    /*sel_LS=*/RC_NONE,
                             /*sel_LE=*/RC_FROM_S,  /*sel_LW=*/RC_NONE));

    PE_VectorData<DATA_W, VLEN> in_L_N, in_L_S, in_L_E, in_L_W;
    in_L_W[0] = 21;   // la PE co-ubicada escribe su out_W
    in_S[0]   = 33;   // la malla presenta un dato por el sur
    routing_cell_step_local(s, /*rst=*/false, /*enable=*/true, in_N, in_S, in_E, in_W,
                            in_L_N, in_L_S, in_L_E, in_L_W);

    bool pass_local_out = (s.out_N[0].to_int() == 21);
    bool pass_local_in  = (s.out_L_E[0].to_int() == 33);
    bool pass_local_idle = (s.out_L_N[0].to_int() == 0 && s.out_L_S[0].to_int() == 0 &&
                            s.out_L_W[0].to_int() == 0);
    printf("%s out_N = in_L_W (la PE inyecta a la malla)   esperado=21 obtenido=%d\n",
           pass_local_out ? "PASS" : "FAIL", s.out_N[0].to_int());
    printf("%s out_L_E = in_S (la malla entrega a la PE)   esperado=33 obtenido=%d\n",
           pass_local_in ? "PASS" : "FAIL", s.out_L_E[0].to_int());
    printf("%s los otros 3 puertos locales quedan en 0 (RC_NONE)\n", pass_local_idle ? "PASS" : "FAIL");
    ok = ok && pass_local_out && pass_local_in && pass_local_idle;

    // La vista de 4 puertos ata los locales a cero: con el MISMO contexto 1,
    // sel_N=RC_FROM_LW ahora tiene que dar 0 -- es lo que hacia
    // PE_Routing_Cell_HLS al integrar la celda en la malla.
    routing_cell_step(s, /*rst=*/false, /*enable=*/true, in_N, in_S, in_E, in_W);
    bool pass_unattached = (s.out_N[0].to_int() == 0 && s.out_L_E[0].to_int() == 33);
    printf("%s sin PE adjunta (routing_cell_step de 4 puertos) out_N=0 y out_L_E sigue relevando\n",
           pass_unattached ? "PASS" : "FAIL");
    ok = ok && pass_unattached;

    if (ok) printf("\nPASS: Routing_Cell_HLS_C cruza E<-W y W<-E segun el contexto programado, "
                   "y el switch-box de 8 puertos enruta desde/hacia la PE co-ubicada.\n");
    return ok ? 0 : 1;
}
