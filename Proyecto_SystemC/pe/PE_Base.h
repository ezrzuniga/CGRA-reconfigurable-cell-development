// PE_Base.h
// Contrato de puertos uniforme para celdas heterogeneas de CGRA_Mesh_Heterogeneous:
// clk/rst/enable, in_N/S/E/W + out_N/S/E/W tipados sobre el "wire" comun de la malla
// (Link = PE_VectorData<DATA_W,VLEN>, el escalar se trata como caso degenerado del
// vector), e instr_in con la forma canonica de 1 parametro (PE_InstrIn<DATA_W>, la
// misma que ya usa PE_scalar) para que cargar un programa sea identico sin importar
// el tipo concreto de celda.
//
// Convencion en el limite escalar<->wire (una celda escalar concreta la implementa
// en su propio wrapper, no aqui): salida escalar -> wire hace broadcast del valor a
// las VLEN lanes; wire -> entrada escalar toma la lane 0.

#ifndef PE_BASE_H
#define PE_BASE_H

#include <systemc.h>
#include "pe_isa.h"

enum class CellKind { SCALAR, VECTOR, MAC, ROUTING, MEMORY };

template <int DATA_W = 32, int VLEN = 4>
class PE_Base : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;
    typedef PE_InstrIn<DATA_W>          InstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_in<Link>  in_N, in_S, in_E, in_W;
    sc_out<Link> out_N, out_S, out_E, out_W;

    sc_in<InstrIn> instr_in;

    explicit PE_Base(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          instr_in("instr_in")
    {}

    virtual ~PE_Base() {}

    virtual void trace(sc_core::sc_trace_file* tf) const = 0;
};

#endif // PE_BASE_H
