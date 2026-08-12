// PE_MAC_Cell_HLS.h
// Copia casi literal de PE_Vector_Cell_HLS.h (mismo patron de puente),
// sobre PE_MAC_HLS en vez de PE_vector_HLS.

#ifndef PE_MAC_CELL_HLS_H
#define PE_MAC_CELL_HLS_H

#include "../pe_isa.h"
#include "PE_MAC_HLS.h"

template <int DATA_W = 32, int VLEN = 4, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_MAC_Cell_HLS : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_InstrIn<DATA_W>          InstrIn;
    typedef PE_MAC_HLS<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE> Inner;
    typedef typename Inner::InstrIn InnerInstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_in<Link>  in_N, in_S, in_E, in_W;
    sc_out<Link> out_N, out_S, out_E, out_W;

    sc_in<InstrIn> instr_in;

    Inner inner;

    SC_HAS_PROCESS(PE_MAC_Cell_HLS);

    explicit PE_MAC_Cell_HLS(sc_core::sc_module_name name)
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

        inner.instr_in(sig_instr_in);

        SC_METHOD(bridge_instr_in);
        sensitive << instr_in;
    }

    void trace(sc_core::sc_trace_file* tf) const {
        inner.trace(tf);
    }

private:
    sc_signal<InnerInstrIn> sig_instr_in;

    void bridge_instr_in() {
        InstrIn in = instr_in.read();
        InnerInstrIn out;
        out.valid = in.valid;
        out.addr = in.addr;
        out.instr.opcode  = in.instr.opcode;
        out.instr.src_a   = in.instr.src_a;
        out.instr.src_b   = in.instr.src_b;
        out.instr.dst     = in.instr.dst;
        out.instr.reg_a   = in.instr.reg_a;
        out.instr.reg_b   = in.instr.reg_b;
        out.instr.reg_dst = in.instr.reg_dst;
        out.instr.imm     = in.instr.imm;
        sig_instr_in.write(out);
    }
};

#endif // PE_MAC_CELL_HLS_H
