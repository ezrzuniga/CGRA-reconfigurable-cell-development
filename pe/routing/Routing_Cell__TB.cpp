// Routing_Cell__TB.cpp
// Smoke test standalone de Routing_Cell: pass-through W->E y N->L
// simultaneos, freeze con enable=0, RC_NONE deja la salida en 0, rst limpia
// el banco de contextos, y los 4 contextos son independientes (cargar uno
// no pisa los otros 3, y ctx_sel conmuta entre ellos sin recargar config_in).

#include <systemc.h>
#include "Routing_Cell.h"

static PE_VectorData<32, 4> make_vector(int a, int b, int c, int d) {
    PE_VectorData<32, 4> v;
    v.lane[0] = a; v.lane[1] = b; v.lane[2] = c; v.lane[3] = d;
    return v;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<PE_VectorData<32, 4>> in_N, in_S, in_E, in_W, in_L;
    sc_signal<PE_VectorData<32, 4>> out_N, out_S, out_E, out_W, out_L;
    sc_signal<RC_ConfigIn> config_in;
    sc_signal<sc_uint<2>> ctx_sel;

    Routing_Cell<32, 4> rc("rc");
    rc.clk(clk); rc.rst(rst); rc.enable(enable);
    rc.in_N(in_N); rc.in_S(in_S); rc.in_E(in_E); rc.in_W(in_W); rc.in_L(in_L);
    rc.out_N(out_N); rc.out_S(out_S); rc.out_E(out_E); rc.out_W(out_W); rc.out_L(out_L);
    rc.config_in(config_in);
    rc.ctx_sel(ctx_sel);

    sc_trace_file* tf = sc_create_vcd_trace_file("routing_cell_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, in_W, "in_W");
    sc_trace(tf, out_E, "out_E");
    rc.trace(tf);

    bool all_pass = true;
    auto check = [&](const std::string& label, PE_VectorData<32, 4> got, PE_VectorData<32, 4> expected) {
        if (!(got == expected)) {
            cout << "FAIL " << label << ": got " << got << ", expected " << expected << endl;
            all_pass = false;
        } else {
            cout << "PASS " << label << ": " << got << endl;
        }
    };

    // Reset.
    rst.write(true);
    enable.write(false);
    in_N.write(PE_VectorData<32, 4>());
    in_S.write(PE_VectorData<32, 4>());
    in_E.write(PE_VectorData<32, 4>());
    in_W.write(PE_VectorData<32, 4>());
    in_L.write(PE_VectorData<32, 4>());
    config_in.write(RC_ConfigIn());
    ctx_sel.write(0);
    sc_start(20, SC_NS);

    rst.write(false);
    enable.write(true);
    in_W.write(make_vector(1, 2, 3, 4));
    in_N.write(make_vector(9, 9, 9, 9));
    in_S.write(make_vector(7, 7, 7, 7));

    auto load_ctx = [&](int ctx, RC_Config c) {
        RC_ConfigIn cfg;
        cfg.valid = true;
        cfg.ctx = ctx;
        cfg.config = c;
        config_in.write(cfg);
        sc_start(10, SC_NS);
        cfg.valid = false;
        config_in.write(cfg);
        sc_start(10, SC_NS);
    };

    // Contexto 0: E <- W (paso horizontal), L <- N (entrega a la PE local).
    RC_Config ctx0;
    ctx0.sel_E = RC_FROM_W;
    ctx0.sel_L = RC_FROM_N;
    load_ctx(0, ctx0);
    ctx_sel.write(0);
    sc_start(1, SC_NS);  // ctx_sel es combinacional, no necesita flanco de clk

    check("ctx0: W->E pass-through", out_E.read(), make_vector(1, 2, 3, 4));
    check("ctx0: N->L pass-through", out_L.read(), make_vector(9, 9, 9, 9));
    check("ctx0: S sin selector queda en NONE", out_S.read(), PE_VectorData<32, 4>());

    // Contexto 1: E <- S (ruta distinta a la del contexto 0).
    RC_Config ctx1;
    ctx1.sel_E = RC_FROM_S;
    load_ctx(1, ctx1);

    // Cargar el contexto 1 no debe pisar el contexto 0.
    ctx_sel.write(0);
    sc_start(1, SC_NS);
    check("ctx0 intacto tras cargar ctx1", out_E.read(), make_vector(1, 2, 3, 4));

    // ctx_sel conmuta a ctx1 sin recargar config_in.
    ctx_sel.write(1);
    sc_start(1, SC_NS);
    check("ctx1 activo via ctx_sel: S->E", out_E.read(), make_vector(7, 7, 7, 7));

    ctx_sel.write(0);
    sc_start(1, SC_NS);

    // Freeze: enable=0 fuerza todas las salidas a 0 aunque la config siga cargada.
    enable.write(false);
    sc_start(10, SC_NS);
    check("enable=0 fuerza salidas a 0", out_E.read(), PE_VectorData<32, 4>());

    enable.write(true);
    sc_start(10, SC_NS);
    check("al reanudar enable, vuelve el pass-through", out_E.read(), make_vector(1, 2, 3, 4));

    // rst limpia todo el banco de contextos -> todas las salidas vuelven a
    // NONE (0) aunque las entradas sigan teniendo datos, sin importar cual
    // ctx_sel este activo.
    rst.write(true);
    sc_start(10, SC_NS);
    rst.write(false);
    sc_start(10, SC_NS);
    check("rst limpia ctx0 (salida vuelve a 0)", out_E.read(), PE_VectorData<32, 4>());
    ctx_sel.write(1);
    sc_start(1, SC_NS);
    check("rst limpia ctx1 tambien (salida vuelve a 0)", out_E.read(), PE_VectorData<32, 4>());

    sc_close_vcd_trace_file(tf);
    return all_pass ? 0 : 1;
}
