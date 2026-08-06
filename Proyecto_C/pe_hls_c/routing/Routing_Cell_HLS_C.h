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

// Profundidad de las colas de entrada por puerto. 4 flits por FIFO, mismo
// numero que RC_NUM_CONTEXTS por convencion del proyecto (no hay relacion
// funcional entre ambos).
static const int RC_FIFO_DEPTH = 4;

// Cola circular de un puerto: retiene hasta RC_FIFO_DEPTH valores que
// llegaron y todavia no fueron consumidos por ninguna salida. `count` es lo
// que credit_* expone hacia afuera como espacio libre (RC_FIFO_DEPTH -
// count) -- ver el comentario de credit-based flow control en
// routing_cell_step().
template <int DATA_W, int VLEN>
struct RC_Fifo {
    typedef PE_VectorData<DATA_W, VLEN> Link;

    Link       buf[RC_FIFO_DEPTH];
    ap_uint<3> head;
    ap_uint<3> count;

    RC_Fifo() : head(0), count(0) {}

    bool full() const  { return count.to_uint() >= RC_FIFO_DEPTH; }
    bool empty() const { return count.to_uint() == 0; }

    void push(const Link& v) {
        if (full()) return;  // credito agotado: el productor no deberia haber enviado
        ap_uint<3> tail = (head + count) % RC_FIFO_DEPTH;
        buf[tail.to_uint()] = v;
        count = count + 1;
    }

    Link peek() const { return empty() ? Link() : buf[head.to_uint()]; }

    void pop() {
        if (empty()) return;
        head = (head + 1) % RC_FIFO_DEPTH;
        count = count - 1;
    }

    void reset() { head = 0; count = 0; }
};

template <int DATA_W = 32, int VLEN = 8>
struct Routing_Cell_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;

    RC_Config  config_bank[RC_NUM_CONTEXTS];
    ap_uint<2> active_ctx;

    // Colas de entrada (una por direccion de malla) + credito disponible de
    // cada una, expuesto como campo publico de estado (no como puerto de
    // cell_step: ese contrato es compartido por las 5 celdas y no lleva
    // credit_* -- ver el comentario grande en routing_cell_step()).
    RC_Fifo<DATA_W, VLEN> fifo_N, fifo_S, fifo_E, fifo_W;
    ap_uint<3> credit_N, credit_S, credit_E, credit_W;

    Link out_N, out_S, out_E, out_W;

    Routing_Cell_State()
        : active_ctx(0),
          credit_N(RC_FIFO_DEPTH), credit_S(RC_FIFO_DEPTH),
          credit_E(RC_FIFO_DEPTH), credit_W(RC_FIFO_DEPTH) {}
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

// Un ciclo del switch-box, ahora mediado por las 4 colas de entrada en vez
// de leer in_N/S/E/W directo hacia el crossbar:
//
//   1. Empuja lo que llega este ciclo a la cola de cada puerto (si hay
//      espacio -- credito).
//   2. El crossbar lee la CABEZA de cada cola (no el input crudo) para
//      calcular las 4 salidas -- mismo mux combinacional de antes, mismo
//      soporte multicast (dos salidas pueden leer la misma cabeza sin
//      conflicto).
//   3. Cada cola cuya cabeza fue efectivamente consumida por al menos una
//      salida avanza (pop) recien al final del ciclo.
//   4. credit_X = espacio libre restante en la cola X, recalculado al
//      final -- esta es la señal de "control de creditos": un productor
//      corriente arriba puede leerla (via el campo publico del estado) para
//      decidir si le queda permitido enviar el proximo ciclo.
//
// Cuando una cola arranca vacia y se le hace push+pop en el mismo llamado
// (caso estable, sin contencion), el dato sigue apareciendo en la salida en
// el mismo ciclo en que llego -- mismo comportamiento observable que la
// version puramente combinacional anterior. La cola solo introduce latencia
// real (mas de 1 ciclo) cuando una entrada recibe datos mas rapido de lo
// que el crossbar los consume, que es exactamente el caso que el
// buffering+creditos existe para manejar sin perder datos.
template <int DATA_W, int VLEN>
inline void routing_cell_step(Routing_Cell_State<DATA_W, VLEN>& s, bool rst, bool enable,
                               const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                               const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    if (rst) {
        for (int i = 0; i < RC_NUM_CONTEXTS; i++) s.config_bank[i] = RC_Config();
        s.fifo_N.reset(); s.fifo_S.reset(); s.fifo_E.reset(); s.fifo_W.reset();
        s.credit_N = s.credit_S = s.credit_E = s.credit_W = RC_FIFO_DEPTH;
        s.out_N = s.out_S = s.out_E = s.out_W = PE_VectorData<DATA_W, VLEN>();
        // A diferencia de pe_scalar_step/pe_vector_step/pe_mac_step (donde
        // rst solo reinicia `pc`, dejando registros/memoria intactos), aca
        // rst SI debe cortar el ciclo por completo: las colas son estado
        // persistente nuevo (no existian en la version combinacional
        // original) y si el push de abajo corriera en el mismo llamado que
        // el reset, dejaria un residuo (el input de ese mismo ciclo, tipicamente
        // 0) encolado por delante de cualquier dato real que llegue despues.
        return;
    }
    if (!enable) {
        s.out_N = s.out_S = s.out_E = s.out_W = PE_VectorData<DATA_W, VLEN>();
        return;
    }

    s.fifo_N.push(in_N);
    s.fifo_S.push(in_S);
    s.fifo_E.push(in_E);
    s.fifo_W.push(in_W);

    PE_VectorData<DATA_W, VLEN> head_N = s.fifo_N.peek();
    PE_VectorData<DATA_W, VLEN> head_S = s.fifo_S.peek();
    PE_VectorData<DATA_W, VLEN> head_E = s.fifo_E.peek();
    PE_VectorData<DATA_W, VLEN> head_W = s.fifo_W.peek();

    const RC_Config& cfg = s.config_bank[s.active_ctx.to_uint() % RC_NUM_CONTEXTS];
    s.out_N = routing_cell_hls_c_detail::select(cfg.sel_N, head_N, head_S, head_E, head_W);
    s.out_S = routing_cell_hls_c_detail::select(cfg.sel_S, head_N, head_S, head_E, head_W);
    s.out_E = routing_cell_hls_c_detail::select(cfg.sel_E, head_N, head_S, head_E, head_W);
    s.out_W = routing_cell_hls_c_detail::select(cfg.sel_W, head_N, head_S, head_E, head_W);

    bool consumed_N = (cfg.sel_N == RC_FROM_N) || (cfg.sel_S == RC_FROM_N) || (cfg.sel_E == RC_FROM_N) || (cfg.sel_W == RC_FROM_N);
    bool consumed_S = (cfg.sel_N == RC_FROM_S) || (cfg.sel_S == RC_FROM_S) || (cfg.sel_E == RC_FROM_S) || (cfg.sel_W == RC_FROM_S);
    bool consumed_E = (cfg.sel_N == RC_FROM_E) || (cfg.sel_S == RC_FROM_E) || (cfg.sel_E == RC_FROM_E) || (cfg.sel_W == RC_FROM_E);
    bool consumed_W = (cfg.sel_N == RC_FROM_W) || (cfg.sel_S == RC_FROM_W) || (cfg.sel_E == RC_FROM_W) || (cfg.sel_W == RC_FROM_W);

    if (consumed_N) s.fifo_N.pop();
    if (consumed_S) s.fifo_S.pop();
    if (consumed_E) s.fifo_E.pop();
    if (consumed_W) s.fifo_W.pop();

    s.credit_N = RC_FIFO_DEPTH - s.fifo_N.count.to_uint();
    s.credit_S = RC_FIFO_DEPTH - s.fifo_S.count.to_uint();
    s.credit_E = RC_FIFO_DEPTH - s.fifo_E.count.to_uint();
    s.credit_W = RC_FIFO_DEPTH - s.fifo_W.count.to_uint();
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
