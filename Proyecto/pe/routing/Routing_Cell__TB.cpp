// Routing_Cell__TB.cpp
// Smoke test standalone de Routing_Cell (version de 8 puertos: N/S/E/W de
// malla + L_N/L_S/L_E/L_W locales, espejo de PE_Base): pass-through
// W->E y L_N->E simultaneos, freeze con enable=0, RC_NONE deja la salida
// en 0, rst limpia el banco de contextos, y los 4 contextos son
// independientes (cargar uno no pisa los otros 3, ctx_sel conmuta sin
// recargar config_in).

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
    sc_signal<PE_VectorData<32, 4>> in_N, in_S, in_E, in_W;
    sc_signal<PE_VectorData<32, 4>> out_N, out_S, out_E, out_W;
    sc_signal<PE_VectorData<32, 4>> in_L_N, in_L_S, in_L_E, in_L_W;
    sc_signal<PE_VectorData<32, 4>> out_L_N, out_L_S, out_L_E, out_L_W;
    sc_signal<RC_ConfigIn> config_in;
    sc_signal<sc_uint<2>> ctx_sel;

    Routing_Cell<32, 4> rc("rc");
    rc.clk(clk); rc.rst(rst); rc.enable(enable);
    rc.in_N(in_N); rc.in_S(in_S); rc.in_E(in_E); rc.in_W(in_W);
    rc.out_N(out_N); rc.out_S(out_S); rc.out_E(out_E); rc.out_W(out_W);
    rc.in_L_N(in_L_N); rc.in_L_S(in_L_S); rc.in_L_E(in_L_E); rc.in_L_W(in_L_W);
    rc.out_L_N(out_L_N); rc.out_L_S(out_L_S); rc.out_L_E(out_L_E); rc.out_L_W(out_L_W);
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
    in_L_N.write(PE_VectorData<32, 4>());
    in_L_S.write(PE_VectorData<32, 4>());
    in_L_E.write(PE_VectorData<32, 4>());
    in_L_W.write(PE_VectorData<32, 4>());
    config_in.write(RC_ConfigIn());
    ctx_sel.write(0);
    sc_start(20, SC_NS);

    rst.write(false);
    enable.write(true);
    in_W.write(make_vector(1, 2, 3, 4));
    in_L_N.write(make_vector(9, 9, 9, 9));
    in_L_S.write(make_vector(7, 7, 7, 7));

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

    // Contexto 0: E (mesh) <- W (mesh), L_E (hacia la PE) <- L_N (lo que la
    // PE emitio por su out_N).
    RC_Config ctx0;
    ctx0.sel_E = RC_FROM_W;
    ctx0.sel_LE = RC_FROM_LN;
    load_ctx(0, ctx0);
    ctx_sel.write(0);
    sc_start(1, SC_NS);  // ctx_sel es combinacional, no necesita flanco de clk

    check("ctx0: W->E pass-through (malla)", out_E.read(), make_vector(1, 2, 3, 4));
    check("ctx0: L_N->L_E pass-through (hacia la PE)", out_L_E.read(), make_vector(9, 9, 9, 9));
    check("ctx0: S sin selector queda en NONE", out_S.read(), PE_VectorData<32, 4>());

    // Contexto 1: E (mesh) <- L_S (ruta distinta a la del contexto 0).
    RC_Config ctx1;
    ctx1.sel_E = RC_FROM_LS;
    load_ctx(1, ctx1);

    // Cargar el contexto 1 no debe pisar el contexto 0.
    ctx_sel.write(0);
    sc_start(1, SC_NS);
    check("ctx0 intacto tras cargar ctx1", out_E.read(), make_vector(1, 2, 3, 4));

    // ctx_sel conmuta a ctx1 sin recargar config_in.
    ctx_sel.write(1);
    sc_start(1, SC_NS);
    check("ctx1 activo via ctx_sel: L_S->E", out_E.read(), make_vector(7, 7, 7, 7));

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
