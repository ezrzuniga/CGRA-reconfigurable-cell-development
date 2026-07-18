// PE_Scalar_Cell.h
// Envoltorio que expone el contrato uniforme de PE_Base sobre un PE_scalar interno
// sin modificar. clk/rst/enable e instr_in bindean directo (mismos tipos en ambos
// lados). Los 8 puertos de dato necesitan puente porque el wire de la malla es
// PE_VectorData<DATA_W,VLEN> y PE_scalar habla sc_int<DATA_W>: la salida escalar hace
// broadcast a las VLEN lanes, la entrada escalar toma la lane 0.

#ifndef PE_SCALAR_CELL_H
#define PE_SCALAR_CELL_H

#include "PE_Base.h"
#include "../pe_scalar/PE_scalar.h"

template <int DATA_W = 32, int VLEN = 4, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_Scalar_Cell : public PE_Base<DATA_W, VLEN> {
public:
    typedef PE_Base<DATA_W, VLEN> Base;
    typedef typename Base::Link Link;

    PE_scalar<DATA_W, NUM_REGS, INSTR_MEM_SIZE> inner;

    SC_HAS_PROCESS(PE_Scalar_Cell);

    explicit PE_Scalar_Cell(sc_core::sc_module_name name)
        : Base(name), inner("inner")
    {
        inner.clk(this->clk);
        inner.rst(this->rst);
        inner.enable(this->enable);
        inner.instr_in(this->instr_in);

        inner.in_N(sig_in_N); inner.in_S(sig_in_S);
        inner.in_E(sig_in_E); inner.in_W(sig_in_W);
        inner.out_N(sig_out_N); inner.out_S(sig_out_S);
        inner.out_E(sig_out_E); inner.out_W(sig_out_W);

        SC_METHOD(bridge_in_N); this->sensitive << this->in_N;
        SC_METHOD(bridge_in_S); this->sensitive << this->in_S;
        SC_METHOD(bridge_in_E); this->sensitive << this->in_E;
        SC_METHOD(bridge_in_W); this->sensitive << this->in_W;

        SC_METHOD(bridge_out_N); this->sensitive << sig_out_N;
        SC_METHOD(bridge_out_S); this->sensitive << sig_out_S;
        SC_METHOD(bridge_out_E); this->sensitive << sig_out_E;
        SC_METHOD(bridge_out_W); this->sensitive << sig_out_W;
    }

    void trace(sc_core::sc_trace_file* tf) const override {
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

    void bridge_in_N() { sig_in_N.write(this->in_N.read()[0]); }
    void bridge_in_S() { sig_in_S.write(this->in_S.read()[0]); }
    void bridge_in_E() { sig_in_E.write(this->in_E.read()[0]); }
    void bridge_in_W() { sig_in_W.write(this->in_W.read()[0]); }

    void bridge_out_N() { this->out_N.write(broadcast(sig_out_N.read())); }
    void bridge_out_S() { this->out_S.write(broadcast(sig_out_S.read())); }
    void bridge_out_E() { this->out_E.write(broadcast(sig_out_E.read())); }
    void bridge_out_W() { this->out_W.write(broadcast(sig_out_W.read())); }
};

#endif // PE_SCALAR_CELL_H
