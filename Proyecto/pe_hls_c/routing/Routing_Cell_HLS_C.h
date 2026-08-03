// Routing_Cell_HLS_C.h
// Transliteracion a C/C++ puro de pe/routing/Routing_Cell.h +
// PE_Routing_Cell_HLS.h: switch-box de 4 puertos de malla (N/S/E/W), banco
// de RC_NUM_CONTEXTS(4) configuraciones, un mux combinacional por salida.
//
// Se descartan los 4 puertos "locales" (L_N/L_S/L_E/L_W) del Routing_Cell
// original -- pensados para una PE co-ubicada, ninguno de los dos arboles
// SystemC (CGRA_Mesh_Heterogeneous, CGRA_Mesh_Static con CellTs...) los usa
// al integrar esta celda como una posicion mas de la malla.
//
// Comportamiento no obvio preservado del puente PE_Routing_Cell_HLS
// (bridge_instr_in): programar un contexto TAMBIEN lo activa de inmediato --
// routing_cell_program() escribe config_bank[ctx] Y fija active_ctx=ctx en
// la misma llamada (alli, ctx_sel_sig solo se reescribia en una carga
// valida). No hace falta un puerto ctx_sel externo por ciclo: el contexto
// activo es estado persistente de la celda, igual que `pc` en las celdas
// tipo PE.
//
// routing_cell_step() es un mux puramente combinacional sobre
// config_bank[active_ctx]. No necesita ningun caso especial de temporizacion
// para calzar con la disciplina de mesh_step() (tomar snapshot de las
// salidas viejas de TODAS las celdas antes de correr a cualquiera este
// ciclo): esa disciplina ya le da a esta celda (y a cualquier otra) el
// retardo de un ciclo hacia sus vecinos automaticamente -- routing_cell_step
// simplemente lee sus in_N/S/E/W (ya retardados por el framework) y escribe
// out_N/S/E/W este mismo llamado, igual que cualquier otro tipo de celda.

#ifndef ROUTING_CELL_HLS_C_H
#define ROUTING_CELL_HLS_C_H

#include <cstdint>
#include "../pe_isa_hls_c.h"

enum RC_Src {
    RC_NONE   = 0,
    RC_FROM_N = 1,
    RC_FROM_S = 2,
    RC_FROM_E = 3,
    RC_FROM_W = 4
};

struct RC_Config {
    ap_uint<4> sel_N, sel_S, sel_E, sel_W;
    RC_Config() : sel_N(RC_NONE), sel_S(RC_NONE), sel_E(RC_NONE), sel_W(RC_NONE) {}
};

static const int RC_NUM_CONTEXTS = 4;

template <int DATA_W = 32, int VLEN = 4>
struct Routing_Cell_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;

    RC_Config  config_bank[RC_NUM_CONTEXTS];
    ap_uint<2> active_ctx;

    Link out_N, out_S, out_E, out_W;

    Routing_Cell_State() : active_ctx(0) {}
};

// Mismo empaquetado que make_routing_config_instr_hls (pe_hls/routing/
// PE_Routing_Cell_HLS.h): un nibble de 4 bits por salida, N/S/E/W en los 4
// nibbles mas significativos de un imm de 32 bits.
template <int DATA_W = 32>
inline PE_Instruction<DATA_W> make_routing_config_instr_c(ap_uint<4> sel_N, ap_uint<4> sel_S,
                                                            ap_uint<4> sel_E, ap_uint<4> sel_W) {
    PE_Instruction<DATA_W> instr;
    uint32_t imm = (sel_N.to_uint() << 28) | (sel_S.to_uint() << 24) |
                   (sel_E.to_uint() << 20) | (sel_W.to_uint() << 16);
    instr.imm = static_cast<int32_t>(imm);
    return instr;
}

namespace routing_cell_hls_c_detail {

inline ap_uint<4> nibble(ap_uint<32> imm, int idx) {
    int shift = (7 - idx) * 4;
    return ap_uint<4>((imm >> shift) & 0xF);
}

template <int DATA_W, int VLEN>
inline PE_VectorData<DATA_W, VLEN> select(
    ap_uint<4> sel,
    const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
    const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    switch (sel) {
        case RC_FROM_N: return in_N;
        case RC_FROM_S: return in_S;
        case RC_FROM_E: return in_E;
        case RC_FROM_W: return in_W;
        default:        return PE_VectorData<DATA_W, VLEN>();  // RC_NONE
    }
}

} // namespace routing_cell_hls_c_detail

// Escribe config_bank[slot % RC_NUM_CONTEXTS] con los 4 selectores
// decodificados de instr.imm, y activa ese contexto de inmediato.
template <int DATA_W, int VLEN>
inline void routing_cell_program(Routing_Cell_State<DATA_W, VLEN>& s,
                                  ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    ap_uint<2> ctx = slot.to_uint() % RC_NUM_CONTEXTS;
    ap_uint<32> imm = ap_uint<32>(ap_int<32>(instr.imm));

    RC_Config cfg;
    cfg.sel_N = routing_cell_hls_c_detail::nibble(imm, 0);
    cfg.sel_S = routing_cell_hls_c_detail::nibble(imm, 1);
    cfg.sel_E = routing_cell_hls_c_detail::nibble(imm, 2);
    cfg.sel_W = routing_cell_hls_c_detail::nibble(imm, 3);

    s.config_bank[ctx] = cfg;
    s.active_ctx = ctx;
}

// Sin acumulador -- no-op (ver PE_Scalar_HLS_C.h).
template <int DATA_W, int VLEN>
inline void routing_cell_clear_acc(Routing_Cell_State<DATA_W, VLEN>&) {}

template <int DATA_W, int VLEN>
inline void routing_cell_step(Routing_Cell_State<DATA_W, VLEN>& s, bool rst, bool enable,
                               const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                               const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    if (rst) {
        for (int i = 0; i < RC_NUM_CONTEXTS; i++) s.config_bank[i] = RC_Config();
    }
    if (!enable) {
        s.out_N = s.out_S = s.out_E = s.out_W = PE_VectorData<DATA_W, VLEN>();
        return;
    }

    const RC_Config& cfg = s.config_bank[s.active_ctx.to_uint() % RC_NUM_CONTEXTS];
    s.out_N = routing_cell_hls_c_detail::select(cfg.sel_N, in_N, in_S, in_E, in_W);
    s.out_S = routing_cell_hls_c_detail::select(cfg.sel_S, in_N, in_S, in_E, in_W);
    s.out_E = routing_cell_hls_c_detail::select(cfg.sel_E, in_N, in_S, in_E, in_W);
    s.out_W = routing_cell_hls_c_detail::select(cfg.sel_W, in_N, in_S, in_E, in_W);
}

// Overloads genericos para el dispatch de la malla heterogenea.
template <int DATA_W, int VLEN>
inline void cell_step(Routing_Cell_State<DATA_W, VLEN>& s, bool rst, bool enable,
                       const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                       const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    routing_cell_step(s, rst, enable, in_N, in_S, in_E, in_W);
}

template <int DATA_W, int VLEN>
inline void cell_program(Routing_Cell_State<DATA_W, VLEN>& s, ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    routing_cell_program(s, slot, instr);
}

template <int DATA_W, int VLEN>
inline void cell_clear_acc(Routing_Cell_State<DATA_W, VLEN>& s)
{
    routing_cell_clear_acc(s);
}

#endif // ROUTING_CELL_HLS_C_H
