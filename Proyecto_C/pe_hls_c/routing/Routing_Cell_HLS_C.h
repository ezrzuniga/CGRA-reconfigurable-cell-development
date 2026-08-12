// Routing_Cell_HLS_C.h
// Transliteracion a C/C++ puro de pe/routing/Routing_Cell.h +
// PE_Routing_Cell_HLS.h: switch-box de 4 puertos de malla (N/S/E/W), banco
// de RC_NUM_CONTEXTS(4) configuraciones, un mux combinacional por salida.
//
// Los 4 puertos "locales" (L_N/L_S/L_E/L_W) del Routing_Cell original SI se
// conservan: son el espejo 1:1 de PE_Base pensado para una PE co-ubicada
// (in_L_X recibe lo que la PE escribe en su out_X, out_L_X alimenta lo que la
// PE lee en su in_X), o sea el switch-box completo de 8 puertos. Ninguna de
// las dos mallas de este repo los cablea todavia -- PE_Routing_Cell_HLS los
// ataba a sc_signal sueltos y CGRA_Mesh_Static_C solo pasa los 4 de malla --
// pero son parte del contrato de la celda y de la codificacion de imm (8
// nibbles), asi que dejarlos afuera habria sido perder capacidad de la
// arquitectura, no simplificar una envoltura de SystemC.
//
// Por eso hay DOS entradas al datapath, y la diferencia importa al integrar:
//   routing_cell_step_local(): el switch-box real de 8 puertos.
//   routing_cell_step():       envoltorio de 4 puertos (los locales atados a
//                              cero), que es la firma que la malla heterogenea
//                              espera de toda celda -- equivale exactamente a
//                              lo que hacia PE_Routing_Cell_HLS al instanciar
//                              esta celda como una posicion mas de la malla.
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
    RC_NONE    = 0,
    RC_FROM_N  = 1,
    RC_FROM_S  = 2,
    RC_FROM_E  = 3,
    RC_FROM_W  = 4,
    RC_FROM_LN = 5,
    RC_FROM_LS = 6,
    RC_FROM_LE = 7,
    RC_FROM_LW = 8
};

// Un selector por cada una de las 8 salidas; sel_X escoge cual de las 8
// entradas alimenta la salida X.
struct RC_Config {
    ap_uint<4> sel_N, sel_S, sel_E, sel_W;
    ap_uint<4> sel_LN, sel_LS, sel_LE, sel_LW;
    RC_Config()
        : sel_N(RC_NONE), sel_S(RC_NONE), sel_E(RC_NONE), sel_W(RC_NONE),
          sel_LN(RC_NONE), sel_LS(RC_NONE), sel_LE(RC_NONE), sel_LW(RC_NONE) {}
};

static const int RC_NUM_CONTEXTS = 4;

template <int DATA_W = 32, int VLEN = 4>
struct Routing_Cell_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;

    RC_Config  config_bank[RC_NUM_CONTEXTS];
    ap_uint<2> active_ctx;
    // Memoria de configuracion del switch-box: los 4 selectores del contexto
    // activo se leen los 4 en el mismo ciclo (un mux por salida), asi que el
    // banco tiene que ser registros y no una RAM de 1-2 puertos -- si no, los
    // 4 muxes se serializan y la celda de routing deja de ser un relevo de 1
    // ciclo. Son 4 contextos x 16 bits: costo despreciable.
#pragma HLS ARRAY_PARTITION variable=config_bank complete dim=1

    Link out_N, out_S, out_E, out_W;
    Link out_L_N, out_L_S, out_L_E, out_L_W;   // hacia la PE co-ubicada

    Routing_Cell_State() : active_ctx(0) {}
};

// Mismo empaquetado que make_routing_config_instr_hls (pe_hls/routing/
// PE_Routing_Cell_HLS.h): un nibble de 4 bits por salida, N/S/E/W en los 4
// nibbles mas significativos y LN/LS/LE/LW en los 4 menos significativos de un
// imm de 32 bits. Los 4 selectores locales van por defecto en RC_NONE, asi que
// una llamada de 4 argumentos (el caso de una celda usada como posicion de la
// malla, sin PE co-ubicada) sigue siendo valida y significa lo mismo que antes.
template <int DATA_W = 32>
inline PE_Instruction<DATA_W> make_routing_config_instr_c(ap_uint<4> sel_N, ap_uint<4> sel_S,
                                                            ap_uint<4> sel_E, ap_uint<4> sel_W,
                                                            ap_uint<4> sel_LN = RC_NONE,
                                                            ap_uint<4> sel_LS = RC_NONE,
                                                            ap_uint<4> sel_LE = RC_NONE,
                                                            ap_uint<4> sel_LW = RC_NONE) {
    PE_Instruction<DATA_W> instr;
    uint32_t imm = (sel_N.to_uint()  << 28) | (sel_S.to_uint()  << 24) |
                   (sel_E.to_uint()  << 20) | (sel_W.to_uint()  << 16) |
                   (sel_LN.to_uint() << 12) | (sel_LS.to_uint() << 8)  |
                   (sel_LE.to_uint() << 4)  | (sel_LW.to_uint());
    instr.imm = static_cast<int32_t>(imm);
    return instr;
}

namespace routing_cell_hls_c_detail {

inline ap_uint<4> nibble(ap_uint<32> imm, int idx) {
#pragma HLS INLINE
    int shift = (7 - idx) * 4;
    return ap_uint<4>((imm >> shift) & 0xF);
}

template <int DATA_W, int VLEN>
inline PE_VectorData<DATA_W, VLEN> select(
    ap_uint<4> sel,
    const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
    const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W,
    const PE_VectorData<DATA_W, VLEN>& in_L_N, const PE_VectorData<DATA_W, VLEN>& in_L_S,
    const PE_VectorData<DATA_W, VLEN>& in_L_E, const PE_VectorData<DATA_W, VLEN>& in_L_W)
{
#pragma HLS INLINE
    switch (sel) {
        case RC_FROM_N:  return in_N;
        case RC_FROM_S:  return in_S;
        case RC_FROM_E:  return in_E;
        case RC_FROM_W:  return in_W;
        case RC_FROM_LN: return in_L_N;
        case RC_FROM_LS: return in_L_S;
        case RC_FROM_LE: return in_L_E;
        case RC_FROM_LW: return in_L_W;
        default:         return PE_VectorData<DATA_W, VLEN>();  // RC_NONE
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
    cfg.sel_N  = routing_cell_hls_c_detail::nibble(imm, 0);
    cfg.sel_S  = routing_cell_hls_c_detail::nibble(imm, 1);
    cfg.sel_E  = routing_cell_hls_c_detail::nibble(imm, 2);
    cfg.sel_W  = routing_cell_hls_c_detail::nibble(imm, 3);
    cfg.sel_LN = routing_cell_hls_c_detail::nibble(imm, 4);
    cfg.sel_LS = routing_cell_hls_c_detail::nibble(imm, 5);
    cfg.sel_LE = routing_cell_hls_c_detail::nibble(imm, 6);
    cfg.sel_LW = routing_cell_hls_c_detail::nibble(imm, 7);

    s.config_bank[ctx] = cfg;
    s.active_ctx = ctx;
}

// Sin acumulador -- no-op (ver PE_Scalar_HLS_C.h).
template <int DATA_W, int VLEN>
inline void routing_cell_clear_acc(Routing_Cell_State<DATA_W, VLEN>&) {}

// Switch-box completo de 8 puertos (4 de malla + 4 locales hacia la PE
// co-ubicada) -- equivalente 1:1 de Routing_Cell::route().
template <int DATA_W, int VLEN>
inline void routing_cell_step_local(Routing_Cell_State<DATA_W, VLEN>& s, bool rst, bool enable,
                                     const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                                     const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W,
                                     const PE_VectorData<DATA_W, VLEN>& in_L_N, const PE_VectorData<DATA_W, VLEN>& in_L_S,
                                     const PE_VectorData<DATA_W, VLEN>& in_L_E, const PE_VectorData<DATA_W, VLEN>& in_L_W)
{
    // Los 8 muxes de salida son independientes entre si: se evaluan en
    // paralelo dentro del mismo ciclo (II=1), igual que el switch-box
    // combinacional del original.
#pragma HLS INLINE
#pragma HLS PIPELINE II=1
    typedef PE_VectorData<DATA_W, VLEN> Link;
    using routing_cell_hls_c_detail::select;

    if (rst) {
    rc_clear_ctx_loop:
        for (int i = 0; i < RC_NUM_CONTEXTS; i++) {
#pragma HLS UNROLL
            s.config_bank[i] = RC_Config();
        }
    }
    if (!enable) {
        s.out_N = s.out_S = s.out_E = s.out_W = Link();
        s.out_L_N = s.out_L_S = s.out_L_E = s.out_L_W = Link();
        return;
    }

    const RC_Config& cfg = s.config_bank[s.active_ctx.to_uint() % RC_NUM_CONTEXTS];
    s.out_N   = select(cfg.sel_N,  in_N, in_S, in_E, in_W, in_L_N, in_L_S, in_L_E, in_L_W);
    s.out_S   = select(cfg.sel_S,  in_N, in_S, in_E, in_W, in_L_N, in_L_S, in_L_E, in_L_W);
    s.out_E   = select(cfg.sel_E,  in_N, in_S, in_E, in_W, in_L_N, in_L_S, in_L_E, in_L_W);
    s.out_W   = select(cfg.sel_W,  in_N, in_S, in_E, in_W, in_L_N, in_L_S, in_L_E, in_L_W);
    s.out_L_N = select(cfg.sel_LN, in_N, in_S, in_E, in_W, in_L_N, in_L_S, in_L_E, in_L_W);
    s.out_L_S = select(cfg.sel_LS, in_N, in_S, in_E, in_W, in_L_N, in_L_S, in_L_E, in_L_W);
    s.out_L_E = select(cfg.sel_LE, in_N, in_S, in_E, in_W, in_L_N, in_L_S, in_L_E, in_L_W);
    s.out_L_W = select(cfg.sel_LW, in_N, in_S, in_E, in_W, in_L_N, in_L_S, in_L_E, in_L_W);
}

// Vista de 4 puertos: la celda integrada como una posicion mas de la malla,
// sin PE co-ubicada -- los 4 puertos locales quedan atados a cero, exactamente
// como los ataba PE_Routing_Cell_HLS con sus sc_signal sueltos.
template <int DATA_W, int VLEN>
inline void routing_cell_step(Routing_Cell_State<DATA_W, VLEN>& s, bool rst, bool enable,
                               const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                               const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
#pragma HLS INLINE
    PE_VectorData<DATA_W, VLEN> unattached;
    routing_cell_step_local(s, rst, enable, in_N, in_S, in_E, in_W,
                            unattached, unattached, unattached, unattached);
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
