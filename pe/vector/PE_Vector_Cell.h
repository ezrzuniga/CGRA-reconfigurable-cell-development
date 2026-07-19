// PE_Vector_Cell.h
// Envoltorio que expone el contrato uniforme de PE_Base sobre un PE_vector interno
// sin modificar. Los puertos de dato bindean directo (el Link de PE_Base ya es
// PE_VectorData<DATA_W,VLEN>, el tipo nativo de PE_vector); solo instr_in necesita
// puente, porque PE_vector espera PE_InstrIn<DATA_W,VLEN> y PE_Base usa la forma
// canonica de 1 parametro (igual a la de PE_scalar) para que cargar un programa sea
// identico sin importar el tipo de celda.

#ifndef PE_VECTOR_CELL_H
#define PE_VECTOR_CELL_H

#include "../PE_Base.h"
#include "PE_vector.h"

template <int DATA_W = 32, int VLEN = 4, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_Vector_Cell : public PE_Base<DATA_W, VLEN> {
public:
    typedef PE_Base<DATA_W, VLEN> Base;
    typedef PE_vector<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE> Inner;
    typedef typename Inner::InstrIn InnerInstrIn;

    Inner inner;

    SC_HAS_PROCESS(PE_Vector_Cell);

    explicit PE_Vector_Cell(sc_core::sc_module_name name)
        : Base(name), inner("inner")
    {
        inner.clk(this->clk);
        inner.rst(this->rst);
        inner.enable(this->enable);

        inner.in_N(this->in_N); inner.in_S(this->in_S);
        inner.in_E(this->in_E); inner.in_W(this->in_W);
        inner.out_N(this->out_N); inner.out_S(this->out_S);
        inner.out_E(this->out_E); inner.out_W(this->out_W);

        inner.instr_in(sig_instr_in);

        SC_METHOD(bridge_instr_in);
        this->sensitive << this->instr_in;
    }

    void trace(sc_core::sc_trace_file* tf) const override {
        inner.trace(tf);
    }

private:
    sc_signal<InnerInstrIn> sig_instr_in;

    void bridge_instr_in() {
        typename Base::InstrIn in = this->instr_in.read();
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

#endif // PE_VECTOR_CELL_H
