// CGRA_Mesh_Static.h
// Version HLS-compatible de mesh/CGRA_Mesh_Heterogeneous.h: mismo wiring
// estatico (loops sobre ROWS/COLS con limites de compilacion, sc_signal
// internas, sin procesos propios), pero el tipo concreto de cada celda se
// fija por una lista de TIPOS en el template (CellTs...), no por un
// std::vector<CellKind> que llega en runtime al constructor. Sin new, sin
// base abstracta, sin vtable: cada celda es un miembro concreto, construido
// in place.
//
// Contrato de puertos que cada CellT debe cumplir (por forma, no por
// herencia -- no hay clase base virtual en este arbol): mismo shape que
// pe/PE_Base.h --
//   sc_in<bool> clk, rst, enable;
//   sc_in<Link> in_N, in_S, in_E, in_W;   sc_out<Link> out_N, out_S, out_E, out_W;
//   sc_in<InstrIn> instr_in;
//   void trace(sc_core::sc_trace_file*) const;
// Un CellT que no cumpla el contrato falla dentro de wire_one<I>() con un
// error de template (no hay un tipo base que lo detecte antes).
//
// Almacenamiento: una cadena de herencia recursiva (CellChain), no
// std::tuple<CellTs...>. sc_module no es copiable/movible, y
// std::tuple<CellTs...>(sc_module_name(...)...) evalua TODOS los argumentos
// (empuja TODOS los nombres a la pila interna de SystemC) antes de que el
// propio constructor de tuple consuma ninguno -- rompe la disciplina de
// push-uno/consume-uno que sc_module_name necesita (confirmado con un
// programa minimo antes de escribir este archivo: falla en runtime con
// "sc_module_name parameter... required"). La cadena de herencia evita el
// problema: cada nivel construye su propia celda con un unico
// mem-initializer (push+construccion+consumo atomico), y cada nivel
// completo antes de pasar al siguiente.

#ifndef CGRA_MESH_STATIC_H
#define CGRA_MESH_STATIC_H

#include <systemc.h>
#include <string>
#include <cstddef>
#include <utility>
#include "../pe/pe_isa.h"

namespace cgra_mesh_static_detail {

template <int COLS>
inline std::string cell_name(std::size_t i) {
    return "pe_" + std::to_string(i / COLS) + "_" + std::to_string(i % COLS);
}

template <std::size_t I, int COLS, typename... CellTs>
struct CellChain;

template <std::size_t I, int COLS>
struct CellChain<I, COLS> {
    CellChain() {}
};

template <std::size_t I, int COLS, typename Head, typename... Tail>
struct CellChain<I, COLS, Head, Tail...> : public CellChain<I + 1, COLS, Tail...> {
    Head cell;

    CellChain()
        : CellChain<I + 1, COLS, Tail...>(),
          cell(sc_core::sc_module_name(cell_name<COLS>(I).c_str()))
    {}
};

template <std::size_t J, std::size_t I, int COLS, typename... CellTs>
struct Getter;

template <std::size_t I, int COLS, typename Head, typename... Tail>
struct Getter<I, I, COLS, Head, Tail...> {
    static Head& get(CellChain<I, COLS, Head, Tail...>& chain) { return chain.cell; }
};

template <std::size_t J, std::size_t I, int COLS, typename Head, typename... Tail>
struct Getter<J, I, COLS, Head, Tail...> {
    static auto& get(CellChain<I, COLS, Head, Tail...>& chain) {
        return Getter<J, I + 1, COLS, Tail...>::get(chain);
    }
};

} // namespace cgra_mesh_static_detail

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
class CGRA_Mesh_Static : public sc_core::sc_module {
    static_assert(sizeof...(CellTs) == ROWS * COLS,
                  "CGRA_Mesh_Static: la lista de tipos de celda debe tener ROWS*COLS elementos");
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;
    typedef PE_InstrIn<DATA_W>          InstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_vector<sc_in<Link>>  in_N,  in_S;   // tamano COLS
    sc_vector<sc_out<Link>> out_N, out_S;  // tamano COLS
    sc_vector<sc_in<Link>>  in_W,  in_E;   // tamano ROWS
    sc_vector<sc_out<Link>> out_W, out_E;  // tamano ROWS

    cgra_mesh_static_detail::CellChain<0, COLS, CellTs...> pe;

    explicit CGRA_Mesh_Static(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), out_N("out_N"), out_S("out_S"),
          in_W("in_W"), in_E("in_E"), out_W("out_W"), out_E("out_E"),
          pe()
    {
        in_N.init(COLS);  out_N.init(COLS);
        in_S.init(COLS);  out_S.init(COLS);
        in_W.init(ROWS);  out_W.init(ROWS);
        in_E.init(ROWS);  out_E.init(ROWS);

        instr_sig.init(ROWS * COLS);
        sig_horiz_e.init(ROWS * (COLS > 1 ? COLS - 1 : 0));
        sig_horiz_w.init(ROWS * (COLS > 1 ? COLS - 1 : 0));
        sig_vert_s.init((ROWS > 1 ? ROWS - 1 : 0) * COLS);
        sig_vert_n.init((ROWS > 1 ? ROWS - 1 : 0) * COLS);

        wire_all(std::make_index_sequence<sizeof...(CellTs)>{});
    }

    void load_instr(int row, int col, sc_dt::sc_uint<8> addr, const Instr& instr) {
        InstrIn in;
        in.valid = true;
        in.addr = addr;
        in.instr = instr;
        instr_sig[row * COLS + col].write(in);
    }

    void clear_instr(int row, int col) {
        instr_sig[row * COLS + col].write(InstrIn());
    }

    // Acceso tipado a una celda por indice (row*COLS+col), en vez de un
    // static_cast sobre una base virtual como en la malla runtime.
    template <std::size_t I>
    auto& cell() {
        return cgra_mesh_static_detail::Getter<I, 0, COLS, CellTs...>::get(pe);
    }

    void trace(sc_core::sc_trace_file* tf) const {
        trace_all(tf, std::make_index_sequence<sizeof...(CellTs)>{});
    }

private:
    sc_vector<sc_signal<InstrIn>> instr_sig;

    sc_vector<sc_signal<Link>> sig_horiz_e, sig_horiz_w;
    sc_vector<sc_signal<Link>> sig_vert_s, sig_vert_n;

    template <std::size_t... I>
    void wire_all(std::index_sequence<I...>) { (wire_one<I>(), ...); }

    template <std::size_t I>
    void wire_one() {
        constexpr int r = I / COLS;
        constexpr int c = I % COLS;
        auto& cell_ref = cgra_mesh_static_detail::Getter<I, 0, COLS, CellTs...>::get(pe);

        cell_ref.clk(clk);
        cell_ref.rst(rst);
        cell_ref.enable(enable);
        cell_ref.instr_in(instr_sig[I]);

        if constexpr (r == 0) {
            cell_ref.in_N(in_N[c]); cell_ref.out_N(out_N[c]);
        } else {
            cell_ref.in_N(sig_vert_s[(r - 1) * COLS + c]); cell_ref.out_N(sig_vert_n[(r - 1) * COLS + c]);
        }

        if constexpr (r == ROWS - 1) {
            cell_ref.in_S(in_S[c]); cell_ref.out_S(out_S[c]);
        } else {
            cell_ref.in_S(sig_vert_n[r * COLS + c]); cell_ref.out_S(sig_vert_s[r * COLS + c]);
        }

        if constexpr (c == 0) {
            cell_ref.in_W(in_W[r]); cell_ref.out_W(out_W[r]);
        } else {
            cell_ref.in_W(sig_horiz_e[r * (COLS - 1) + (c - 1)]); cell_ref.out_W(sig_horiz_w[r * (COLS - 1) + (c - 1)]);
        }

        if constexpr (c == COLS - 1) {
            cell_ref.in_E(in_E[r]); cell_ref.out_E(out_E[r]);
        } else {
            cell_ref.in_E(sig_horiz_w[r * (COLS - 1) + c]); cell_ref.out_E(sig_horiz_e[r * (COLS - 1) + c]);
        }
    }

    template <std::size_t... I>
    void trace_all(sc_core::sc_trace_file* tf, std::index_sequence<I...>) const {
        (cgra_mesh_static_detail::Getter<I, 0, COLS, CellTs...>::get(const_cast<decltype(pe)&>(pe)).trace(tf), ...);
    }
};

#endif // CGRA_MESH_STATIC_H
