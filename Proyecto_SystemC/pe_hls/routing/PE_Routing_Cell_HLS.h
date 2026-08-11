// PE_Routing_Cell_HLS.h
// Mismo patron de puente que pe/routing/PE_Routing_Cell.h, sobre el
// Routing_Cell.h existente sin modificar (ya es sintetizable tal cual, ver
// plan de HLS). Sin herencia de base abstracta.

#ifndef PE_ROUTING_CELL_HLS_H
#define PE_ROUTING_CELL_HLS_H

#include "../pe_isa.h"
#include "Routing_Cell.h"

// Mismo empaquetado que make_routing_config_instr en pe/routing/PE_Routing_Cell.h
// (N,S,E,W,LN,LS,LE,LW, nibble mas significativo primero), reimplementado aca
// para que este arbol no dependa de pe/routing/PE_Routing_Cell.h.
template <int DATA_W = 32>
inline PE_Instruction<DATA_W> make_routing_config_instr_hls(sc_uint<4> sel_N, sc_uint<4> sel_S,
                                                              sc_uint<4> sel_E, sc_uint<4> sel_W,
                                                              sc_uint<4> sel_LN = RC_NONE,
                                                              sc_uint<4> sel_LS = RC_NONE,
                                                              sc_uint<4> sel_LE = RC_NONE,
                                                              sc_uint<4> sel_LW = RC_NONE) {
    PE_Instruction<DATA_W> instr;
    uint32_t imm = (sel_N.to_uint()  << 28) | (sel_S.to_uint()  << 24) |
                   (sel_E.to_uint()  << 20) | (sel_W.to_uint()  << 16) |
                   (sel_LN.to_uint() << 12) | (sel_LS.to_uint() << 8)  |
                   (sel_LE.to_uint() << 4)  | (sel_LW.to_uint());
    instr.imm = static_cast<int32_t>(imm);
    return instr;
}

template <int DATA_W = 32, int VLEN = 4>
class PE_Routing_Cell_HLS : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_InstrIn<DATA_W>          InstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_in<Link>  in_N, in_S, in_E, in_W;
    sc_out<Link> out_N, out_S, out_E, out_W;

    sc_in<InstrIn> instr_in;

    Routing_Cell<DATA_W, VLEN> inner;

    SC_HAS_PROCESS(PE_Routing_Cell_HLS);

    explicit PE_Routing_Cell_HLS(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          instr_in("instr_in"),
          inner("inner")
    {
        inner.clk(clk);
        inner.rst(rst);
        inner.enable(enable);

        inner.in_N(in_N); inner.in_S(in_S);
        inner.in_E(in_E); inner.in_W(in_W);
        inner.out_N(out_N); inner.out_S(out_S);
        inner.out_E(out_E); inner.out_W(out_W);

        // Sin PE local adjunta -- ver comentario en pe/routing/PE_Routing_Cell.h.
        inner.in_L_N(sink_in_L_N); inner.in_L_S(sink_in_L_S);
        inner.in_L_E(sink_in_L_E); inner.in_L_W(sink_in_L_W);
        inner.out_L_N(sink_out_L_N); inner.out_L_S(sink_out_L_S);
        inner.out_L_E(sink_out_L_E); inner.out_L_W(sink_out_L_W);

        inner.config_in(config_in_sig);
        inner.ctx_sel(ctx_sel_sig);

        SC_METHOD(bridge_instr_in);
        sensitive << instr_in;
    }

    void trace(sc_core::sc_trace_file* tf) const {
        inner.trace(tf);
    }

private:
    sc_signal<Link> sink_in_L_N, sink_in_L_S, sink_in_L_E, sink_in_L_W;
    sc_signal<Link> sink_out_L_N, sink_out_L_S, sink_out_L_E, sink_out_L_W;

    sc_signal<RC_ConfigIn> config_in_sig;
    sc_signal<sc_uint<2>>  ctx_sel_sig;

    static sc_uint<4> nibble(sc_int<DATA_W> imm, int idx) {
        int shift = (7 - idx) * 4;
        return sc_uint<4>((imm.to_uint() >> shift) & 0xF);
    }

    // Mismo cuidado que PE_Routing_Cell::bridge_instr_in: ctx_sel solo se
    // reposiciona en una carga realmente valida, nunca en un clear_instr().
    void bridge_instr_in() {
        InstrIn in = instr_in.read();

        RC_ConfigIn cfg_in;
        cfg_in.valid = in.valid;
        cfg_in.ctx = in.addr.to_uint() & 0x3;
        cfg_in.config.sel_N  = nibble(in.instr.imm, 0);
        cfg_in.config.sel_S  = nibble(in.instr.imm, 1);
        cfg_in.config.sel_E  = nibble(in.instr.imm, 2);
        cfg_in.config.sel_W  = nibble(in.instr.imm, 3);
        cfg_in.config.sel_LN = nibble(in.instr.imm, 4);
        cfg_in.config.sel_LS = nibble(in.instr.imm, 5);
        cfg_in.config.sel_LE = nibble(in.instr.imm, 6);
        cfg_in.config.sel_LW = nibble(in.instr.imm, 7);

        if (cfg_in.valid) {
            config_in_sig.write(cfg_in);
            ctx_sel_sig.write(cfg_in.ctx);
        }
    }
};

#endif // PE_ROUTING_CELL_HLS_H
