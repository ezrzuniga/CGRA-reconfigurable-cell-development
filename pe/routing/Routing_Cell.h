// Routing_Cell.h
// Celda de enrutamiento (switch-box) de la CGRA: desacopla el ruteo de datos
// del computo de la PE. Tiene 5 puertos de datos (N/S/E/W hacia celdas
// vecinas + L hacia la PE local) y, para cada salida, un selector estatico
// de 3 bits que escoge entre las otras 4 entradas o "ninguna" (NONE). El
// wire de datos es PE_VectorData<DATA_W,VLEN>, el mismo tipo de
// CGRA_Mesh_Heterogeneous, para que esta celda pueda insertarse entre PE y
// malla sin adaptadores.
//
// Config: banco de RC_NUM_CONTEXTS (4) contextos, cada uno cargado por
// separado via config_in (mismo patron addr->ctx que instr_mem de los PE,
// un flanco de clk para que quede latcheado). ctx_sel escoge cual de los 4
// esta activo en cada ciclo (combinacional, sin flanco de por medio) -- útil
// para alternar entre varios patrones de ruteo pre-cargados sin recargar
// config_in. El mux de cada salida es combinacional sobre el contexto
// activo. No hay arbitraje: dos salidas pueden leer la misma entrada sin
// conflicto (es una copia, no una reserva de recurso).

#ifndef ROUTING_CELL_H
#define ROUTING_CELL_H

#include <systemc.h>
#include "../pe/pe_isa.h"

// Origen posible para cada salida del switch-box. NONE deja esa salida en
// el valor por defecto de Link (todas las lanes en 0).
enum RC_Src {
    RC_NONE = 0,
    RC_FROM_N = 1,
    RC_FROM_S = 2,
    RC_FROM_E = 3,
    RC_FROM_W = 4,
    RC_FROM_L = 5
};

// Config completa de una celda: un selector de 3 bits por cada una de las 5
// salidas. sel_X escoge que entrada alimenta la salida X.
struct RC_Config {
    sc_uint<3> sel_N, sel_S, sel_E, sel_W, sel_L;

    RC_Config()
        : sel_N(RC_NONE), sel_S(RC_NONE), sel_E(RC_NONE),
          sel_W(RC_NONE), sel_L(RC_NONE) {}

    inline bool operator==(const RC_Config& o) const {
        return sel_N == o.sel_N && sel_S == o.sel_S && sel_E == o.sel_E &&
               sel_W == o.sel_W && sel_L == o.sel_L;
    }
};

inline ostream& operator<<(ostream& os, const RC_Config& c) {
    os << "{N=" << c.sel_N << ",S=" << c.sel_S << ",E=" << c.sel_E
       << ",W=" << c.sel_W << ",L=" << c.sel_L << "}";
    return os;
}

inline void sc_trace(sc_core::sc_trace_file* tf, const RC_Config& c, const std::string& name) {
    sc_trace(tf, c.sel_N, name + ".sel_N");
    sc_trace(tf, c.sel_S, name + ".sel_S");
    sc_trace(tf, c.sel_E, name + ".sel_E");
    sc_trace(tf, c.sel_W, name + ".sel_W");
    sc_trace(tf, c.sel_L, name + ".sel_L");
}

// Numero de contextos de configuracion almacenados por celda. Cada contexto
// es un RC_Config completo (5 selectores); cual esta activo lo decide
// ctx_sel en cada ciclo (ver Routing_Cell::ctx_sel mas abajo).
static const int RC_NUM_CONTEXTS = 4;

// Bundle para el puerto unico de carga de config (mismo patron que
// PE_InstrIn, con el campo ctx extra en vez de addr): valid=false no toca
// el banco interno, valid=true escribe `config` en el contexto `ctx` en el
// siguiente flanco de clk. Cada contexto se carga por separado, igual que
// cada direccion de instr_mem en los PE.
struct RC_ConfigIn {
    bool valid;
    sc_uint<2> ctx;
    RC_Config config;

    RC_ConfigIn() : valid(false), ctx(0), config() {}

    inline bool operator==(const RC_ConfigIn& o) const {
        return valid == o.valid && ctx == o.ctx && config == o.config;
    }
};

inline ostream& operator<<(ostream& os, const RC_ConfigIn& in) {
    os << "{valid=" << in.valid << ", ctx=" << in.ctx << ", config=" << in.config << "}";
    return os;
}

inline void sc_trace(sc_core::sc_trace_file* tf, const RC_ConfigIn& in, const std::string& name) {
    sc_trace(tf, in.valid, name + ".valid");
    sc_trace(tf, in.ctx, name + ".ctx");
    sc_trace(tf, in.config, name + ".config");
}

// Celda con banco de RC_NUM_CONTEXTS (4) contextos de configuracion. Cada
// contexto se carga por separado via config_in (mismo patron addr->ctx que
// instr_mem de los PE); cual esta activo en cada ciclo lo decide el puerto
// ctx_sel (combinacional, sin flanco de clk de por medio) -- pensado para
// que el mismo bailout de mux N/S/E/W/L pueda cambiar ciclo a ciclo sin
// tener que recargar config_in, p. ej. para alternar entre 4 patrones de
// ruteo pre-cargados (tiempo compartido tipo TDM).
template <int DATA_W = 32, int VLEN = 4>
class Routing_Cell : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    // Malla (vecinas) + local (PE adjunta a esta celda de enrutamiento).
    sc_in<Link>  in_N, in_S, in_E, in_W, in_L;
    sc_out<Link> out_N, out_S, out_E, out_W, out_L;

    sc_in<RC_ConfigIn> config_in;
    sc_in<sc_uint<2>>  ctx_sel;  // contexto activo (0..RC_NUM_CONTEXTS-1), efecto combinacional

    SC_HAS_PROCESS(Routing_Cell);

    explicit Routing_Cell(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"), in_L("in_L"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"), out_L("out_L"),
          config_in("config_in"), ctx_sel("ctx_sel")
    {
        config_bank.init(RC_NUM_CONTEXTS);

        SC_METHOD(load_config);
        sensitive << clk.pos();

        SC_METHOD(route);
        sensitive << enable << ctx_sel
                  << in_N << in_S << in_E << in_W << in_L;
        for (int i = 0; i < RC_NUM_CONTEXTS; i++) sensitive << config_bank[i];
    }

    void trace(sc_core::sc_trace_file* tf) const {
        for (int i = 0; i < RC_NUM_CONTEXTS; i++) {
            sc_trace(tf, config_bank[i], std::string(name()) + ".config_bank_" + std::to_string(i));
        }
        sc_trace(tf, ctx_sel, std::string(name()) + ".ctx_sel");
        sc_trace(tf, out_N, std::string(name()) + ".out_N");
        sc_trace(tf, out_S, std::string(name()) + ".out_S");
        sc_trace(tf, out_E, std::string(name()) + ".out_E");
        sc_trace(tf, out_W, std::string(name()) + ".out_W");
        sc_trace(tf, out_L, std::string(name()) + ".out_L");
    }

private:
    sc_vector<sc_signal<RC_Config>> config_bank;  // tamano RC_NUM_CONTEXTS

    void load_config() {
        if (rst.read()) {
            for (int i = 0; i < RC_NUM_CONTEXTS; i++) config_bank[i].write(RC_Config());
            return;
        }
        RC_ConfigIn in = config_in.read();
        if (in.valid) {
            config_bank[in.ctx].write(in.config);
        }
    }

    Link select(sc_uint<3> sel) const {
        switch (sel) {
            case RC_FROM_N: return in_N.read();
            case RC_FROM_S: return in_S.read();
            case RC_FROM_E: return in_E.read();
            case RC_FROM_W: return in_W.read();
            case RC_FROM_L: return in_L.read();
            default:        return Link();  // RC_NONE
        }
    }

    void route() {
        if (!enable.read()) {
            out_N.write(Link());
            out_S.write(Link());
            out_E.write(Link());
            out_W.write(Link());
            out_L.write(Link());
            return;
        }
        RC_Config cfg = config_bank[ctx_sel.read()].read();
        out_N.write(select(cfg.sel_N));
        out_S.write(select(cfg.sel_S));
        out_E.write(select(cfg.sel_E));
        out_W.write(select(cfg.sel_W));
        out_L.write(select(cfg.sel_L));
    }
};

#endif // ROUTING_CELL_H
