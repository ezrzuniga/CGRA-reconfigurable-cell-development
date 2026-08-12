// PE_Scalar_Cell_HLS.h
// Igual patron de puente que pe/scalar/PE_Scalar_Cell.h (broadcast a las
// VLEN lanes en la salida, lane 0 en la entrada), sobre PE_scalar_HLS en vez
// de PE_scalar. No hereda de una base abstracta: el contrato de puertos
// (clk/rst/enable, in_N/S/E/W y out_N/S/E/W tipo Link, instr_in tipo
// InstrIn) se cumple por forma, no por herencia virtual -- ver comentario de
// contrato en mesh_hls/CGRA_Mesh_Static.h.

#ifndef PE_SCALAR_CELL_HLS_H
#define PE_SCALAR_CELL_HLS_H

#include "../pe_isa.h"
#include "PE_scalar_HLS.h"

template <int DATA_W = 32, int VLEN = 4, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_Scalar_Cell_HLS : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_InstrIn<DATA_W>          InstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_in<Link>  in_N, in_S, in_E, in_W;
    sc_out<Link> out_N, out_S, out_E, out_W;

    sc_in<InstrIn> instr_in;

    PE_scalar_HLS<DATA_W, NUM_REGS, INSTR_MEM_SIZE> inner;

    SC_HAS_PROCESS(PE_Scalar_Cell_HLS);

    explicit PE_Scalar_Cell_HLS(sc_core::sc_module_name name)
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
        inner.instr_in(instr_in);

        inner.in_N(sig_in_N); inner.in_S(sig_in_S);
        inner.in_E(sig_in_E); inner.in_W(sig_in_W);
        inner.out_N(sig_out_N); inner.out_S(sig_out_S);
        inner.out_E(sig_out_E); inner.out_W(sig_out_W);

        SC_METHOD(bridge_in_N); sensitive << in_N;
        SC_METHOD(bridge_in_S); sensitive << in_S;
        SC_METHOD(bridge_in_E); sensitive << in_E;
        SC_METHOD(bridge_in_W); sensitive << in_W;

        SC_METHOD(bridge_out_N); sensitive << sig_out_N;
        SC_METHOD(bridge_out_S); sensitive << sig_out_S;
        SC_METHOD(bridge_out_E); sensitive << sig_out_E;
        SC_METHOD(bridge_out_W); sensitive << sig_out_W;
    }

    void trace(sc_core::sc_trace_file* tf) const {
        inner.trace(tf);
    }

private:
    sc_signal<sc_int<DATA_W>> sig_in_N, sig_in_S, sig_in_E, sig_in_W;
    sc_signal<sc_int<DATA_W>> sig_out_N, sig_out_S, sig_out_E, sig_out_W;

    static Link broadcast(sc_int<DATA_W> v) {
        Link r;
        for (int i = 0; i < VLEN; ++i) r[i] = v;
        return r;
    }

    void bridge_in_N() { sig_in_N.write(in_N.read()[0]); }
    void bridge_in_S() { sig_in_S.write(in_S.read()[0]); }
    void bridge_in_E() { sig_in_E.write(in_E.read()[0]); }
    void bridge_in_W() { sig_in_W.write(in_W.read()[0]); }

    void bridge_out_N() { out_N.write(broadcast(sig_out_N.read())); }
    void bridge_out_S() { out_S.write(broadcast(sig_out_S.read())); }
    void bridge_out_E() { out_E.write(broadcast(sig_out_E.read())); }
    void bridge_out_W() { out_W.write(broadcast(sig_out_W.read())); }
};

#endif // PE_SCALAR_CELL_HLS_H
