// PE_Routing_Cell.h
// Envoltorio que expone el contrato uniforme de PE_Base (in/out N/S/E/W tipados
// sobre Link, instr_in de 1 parametro) sobre un Routing_Cell interno sin modificar,
// para que una celda de enrutamiento pueda ocupar una posicion del grid en
// CGRA_Mesh_Heterogeneous igual que PE_Scalar_Cell/PE_Vector_Cell/PE_MAC_Cell.
//
// Puertos de malla (in_N/S/E/W, out_N/S/E/W): bindean directo, mismo tipo Link en
// ambos lados (Routing_Cell ya habla PE_VectorData<DATA_W,VLEN>, no hace falta
// puente).
//
// Puertos locales de Routing_Cell (in_L_X/out_L_X, pensados para una PE adjunta que
// aqui no existe -- esta celda ocupa su propia posicion del grid, no esta pegada a
// otra PE): se atan a Link() fijo (in_L_* sin driver, quedan en su valor de reset
// por default-construction de sc_signal) y a senales sumidero sin leer (out_L_*).
// Cualquier contexto que rutee RC_FROM_L* simplemente entrega Link() -- ruta
// intencionalmente inerte, documentada, no un bug.
//
// Programacion via instr_in (reusa PE_InstrIn<DATA_W> en vez de inventar un puerto
// nuevo, para que cargar un programa sea uniforme en toda la malla igual que con
// las demas celdas): el campo imm (32 bits) empaqueta los 8 selectores de 4 bits de
// un RC_Config (N,S,E,W,LN,LS,LE,LW, en ese orden, nibble mas significativo primero);
// addr[1:0] (de PE_InstrIn) escoge el contexto (0..RC_NUM_CONTEXTS-1) igual que
// selecciona direccion en la memoria de instrucciones de las demas celdas.
// Simplificacion consciente: el contexto que se acaba de cargar se activa de
// inmediato (ctx_sel sigue al mismo addr), no hay una fase separada de "activar sin
// recargar" -- Routing_Cell si la soporta a nivel interno, pero exponer esa
// distincion hubiera requerido un segundo puerto fuera del contrato PE_Base.

#ifndef PE_ROUTING_CELL_H
#define PE_ROUTING_CELL_H

#include "../PE_Base.h"
#include "Routing_Cell.h"

// Empaqueta los 8 selectores de un RC_Config en el campo imm (32 bits) de una
// PE_Instruction<DATA_W>, mismo orden que PE_Routing_Cell::bridge_instr_in espera
// (N,S,E,W,LN,LS,LE,LW, nibble mas significativo primero). Helper compartido para
// que cualquier orquestador (testbenches, MeshWrapper) programe una celda de
// enrutamiento sin reimplementar el empaquetado.
template <int DATA_W = 32>
inline PE_Instruction<DATA_W> make_routing_config_instr(sc_uint<4> sel_N, sc_uint<4> sel_S,
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
class PE_Routing_Cell : public PE_Base<DATA_W, VLEN> {
public:
    typedef PE_Base<DATA_W, VLEN> Base;
    typedef typename Base::Link Link;

    Routing_Cell<DATA_W, VLEN> inner;

    SC_HAS_PROCESS(PE_Routing_Cell);

    explicit PE_Routing_Cell(sc_core::sc_module_name name)
        : Base(name), inner("inner")
    {
        inner.clk(this->clk);
        inner.rst(this->rst);
        inner.enable(this->enable);

        inner.in_N(this->in_N); inner.in_S(this->in_S);
        inner.in_E(this->in_E); inner.in_W(this->in_W);
        inner.out_N(this->out_N); inner.out_S(this->out_S);
        inner.out_E(this->out_E); inner.out_W(this->out_W);

        // Sin PE local adjunta: entradas locales fijas en Link() (sin driver),
        // salidas locales a senales sumidero que nadie lee.
        inner.in_L_N(sink_in_L_N); inner.in_L_S(sink_in_L_S);
        inner.in_L_E(sink_in_L_E); inner.in_L_W(sink_in_L_W);
        inner.out_L_N(sink_out_L_N); inner.out_L_S(sink_out_L_S);
        inner.out_L_E(sink_out_L_E); inner.out_L_W(sink_out_L_W);

        inner.config_in(config_in_sig);
        inner.ctx_sel(ctx_sel_sig);

        SC_METHOD(bridge_instr_in);
        this->sensitive << this->instr_in;
    }

    void trace(sc_core::sc_trace_file* tf) const override {
        inner.trace(tf);
    }

private:
    sc_signal<Link> sink_in_L_N, sink_in_L_S, sink_in_L_E, sink_in_L_W;
    sc_signal<Link> sink_out_L_N, sink_out_L_S, sink_out_L_E, sink_out_L_W;

    sc_signal<RC_ConfigIn> config_in_sig;
    sc_signal<sc_uint<2>>  ctx_sel_sig;

    static sc_uint<4> nibble(sc_int<DATA_W> imm, int idx) {
        // idx 0 = nibble mas significativo (sel_N) .. idx 7 = menos significativo (sel_LW).
        int shift = (7 - idx) * 4;
        return sc_uint<4>((imm.to_uint() >> shift) & 0xF);
    }

    void bridge_instr_in() {
        typename Base::InstrIn in = this->instr_in.read();

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

        config_in_sig.write(cfg_in);
        ctx_sel_sig.write(cfg_in.ctx);
    }
};

#endif // PE_ROUTING_CELL_H
