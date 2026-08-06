// PE_Memory_HLS_C.h
// Transliteracion a C/C++ puro de memory_hls/PE_Memory_HLS_Cell.h: SRAM +
// motor de rafaga como arreglo fijo + FSM de 2 estados, sin TLM/sc_module.
// Proyecto_SystemC/memory/Access_controller.h se reusa TAL CUAL -- ya es
// C++ puro, sin dependencia de SystemC, nada que portar.
//
// Convenio de campos identico al original (MemCellField,
// make_memory_field_instr): `slot` (el mismo parametro generico de
// <tipo>_program en todas las celdas) hace de indice de contexto (0..3);
// `instr.reg_dst` selecciona el campo, `instr.imm` es el valor.
// reg_dst=MEM_FIELD_START dispara la rafaga sobre el contexto indicado.
//
// memory_program() absorbe la rama ST_IDLE del tick() original (escritura de
// campos, disparo de START) por completo; memory_step() se queda solo con
// el avance de rafaga por ciclo -- misma separacion programar/correr ya
// establecida para las celdas tipo PE (ver PE_MAC_HLS_C.h).
//
// Simplificaciones heredadas del original (documentadas, no bugs): SRAM
// direccionada por palabra (no por byte), unico puerto NoC atado al borde
// oeste (in_W/out_W) -- N/S/E quedan cableados por uniformidad de grilla
// pero siempre en cero.

#ifndef PE_MEMORY_HLS_C_H
#define PE_MEMORY_HLS_C_H

#include "../pe_hls_c/pe_isa_hls_c.h"
#include "../../Proyecto_SystemC/memory/Access_controller.h"

enum MemCellField {
    MEM_FIELD_SRC_ADDR = 0,
    MEM_FIELD_DST_ADDR = 1,
    MEM_FIELD_STRIDE   = 2,
    MEM_FIELD_COUNT    = 3,
    MEM_FIELD_MODE     = 4,
    MEM_FIELD_DIR      = 5,
    MEM_FIELD_START    = 31
};

template <int DATA_W = 32>
inline PE_Instruction<DATA_W> make_memory_field_instr_c(int field, int32_t value) {
    PE_Instruction<DATA_W> instr;
    instr.reg_dst = field;
    instr.imm = value;
    return instr;
}

struct MemContextRegisters {
    uint32_t src_addr = 0;
    uint32_t dst_addr = 0;
    int32_t  stride = 0;
    uint32_t count = 0;
    uint32_t mode = 0;  // 0: Direct, 1: Stride
    uint32_t dir = 0;   // 0: SRAM->NoC(W), 1: NoC(W)->SRAM, 2: SRAM->SRAM
};

enum MemCellFSMState { MEM_ST_IDLE, MEM_ST_BURST };

// Default subido a 1024 palabras (4KB) para caer dentro del rango "1-4KB"
// reclamado en el diagrama de Avance 1 (antes 512 palabras = 2KB, el limite
// inferior del rango, no un punto dentro de el). El unico consumidor real
// hoy (CGRA_Hetero_2x2_Demo_Top_C.h) fija su propio SIZE_WORDS explicito --
// ver el comentario alli sobre por que se subio tambien.
template <int DATA_W = 32, int VLEN = 8, int SIZE_WORDS = 1024>
struct PE_Memory_State {
    typedef PE_VectorData<DATA_W, VLEN> Link;

    ap_int<DATA_W>       sram[SIZE_WORDS];
    MemContextRegisters  contexts[4];

    MemCellFSMState state;
    uint32_t        active_context;
    uint32_t        dir;
    bool            busy, done;
    AccessController src_ac, dst_ac;

    Link out_N, out_S, out_E, out_W;

    PE_Memory_State() : state(MEM_ST_IDLE), active_context(0), dir(0), busy(false), done(false) {
        for (int i = 0; i < SIZE_WORDS; i++) sram[i] = 0;
    }
};

namespace pe_memory_hls_c_detail {

inline void write_context_field(MemContextRegisters& ctx, uint32_t field, uint32_t value) {
    switch (field) {
        case MEM_FIELD_SRC_ADDR: ctx.src_addr = value; break;
        case MEM_FIELD_DST_ADDR: ctx.dst_addr = value; break;
        case MEM_FIELD_STRIDE:   ctx.stride = static_cast<int32_t>(value); break;
        case MEM_FIELD_COUNT:    ctx.count = value; break;
        case MEM_FIELD_MODE:     ctx.mode = value; break;
        case MEM_FIELD_DIR:      ctx.dir = value; break;
        default: break;
    }
}

} // namespace pe_memory_hls_c_detail

// Canal lateral de configuracion: escribe un campo de contexto, o dispara la
// rafaga (reg_dst=MEM_FIELD_START) configurando los AccessController y
// pasando a ST_BURST.
template <int DATA_W, int VLEN, int SIZE_WORDS>
inline void memory_program(PE_Memory_State<DATA_W, VLEN, SIZE_WORDS>& s,
                            ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    uint32_t ctx = slot.to_uint() % 4;
    uint32_t field = instr.reg_dst.to_uint();
    uint32_t value = static_cast<uint32_t>(instr.imm.to_int());

    if (field == MEM_FIELD_START) {
        s.active_context = ctx;
        s.dir = s.contexts[ctx].dir;
        s.src_ac.configure(s.contexts[ctx].src_addr, s.contexts[ctx].stride, s.contexts[ctx].count, s.contexts[ctx].mode);
        s.dst_ac.configure(s.contexts[ctx].dst_addr, s.contexts[ctx].stride, s.contexts[ctx].count, s.contexts[ctx].mode);
        s.state = MEM_ST_BURST;
        s.busy = true;
        s.done = false;
    } else {
        pe_memory_hls_c_detail::write_context_field(s.contexts[ctx], field, value);
    }
}

// Sin acumulador -- no-op (ver PE_Scalar_HLS_C.h).
template <int DATA_W, int VLEN, int SIZE_WORDS>
inline void memory_clear_acc(PE_Memory_State<DATA_W, VLEN, SIZE_WORDS>&) {}

// Un ciclo de reloj: si esta en rafaga, transfiere una palabra (o cierra la
// rafaga si los AccessController ya no tienen mas direcciones). rst NO
// limpia sram/contexts, solo el estado de la FSM (mismo precedente que pc
// en las celdas tipo PE).
template <int DATA_W, int VLEN, int SIZE_WORDS>
inline void memory_step(PE_Memory_State<DATA_W, VLEN, SIZE_WORDS>& s, bool rst, bool enable,
                         const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                         const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    (void)in_N; (void)in_S; (void)in_E;  // puerto NoC unico: solo in_W/out_W

    if (rst) {
        s.state = MEM_ST_IDLE;
        s.busy = false;
        s.done = false;
        return;
    }
    if (!enable) return;

    if (s.state != MEM_ST_BURST) return;  // ST_IDLE: nada que hacer aca, ver memory_program()

    if (!(s.src_ac.has_next() && s.dst_ac.has_next())) {
        s.state = MEM_ST_IDLE;
        s.busy = false;
        s.done = true;
        return;
    }

    uint32_t src = s.src_ac.next_address();
    uint32_t dst = s.dst_ac.next_address();

    if (s.dir == 0) {
        PE_VectorData<DATA_W, VLEN> out = s.out_W;
        out[0] = s.sram[src % SIZE_WORDS];
        s.out_W = out;
    } else if (s.dir == 1) {
        s.sram[dst % SIZE_WORDS] = in_W[0];
    } else if (s.dir == 2) {
        s.sram[dst % SIZE_WORDS] = s.sram[src % SIZE_WORDS];
    }
}

// Overloads genericos para el dispatch de la malla heterogenea.
template <int DATA_W, int VLEN, int SIZE_WORDS>
inline void cell_step(PE_Memory_State<DATA_W, VLEN, SIZE_WORDS>& s, bool rst, bool enable,
                       const PE_VectorData<DATA_W, VLEN>& in_N, const PE_VectorData<DATA_W, VLEN>& in_S,
                       const PE_VectorData<DATA_W, VLEN>& in_E, const PE_VectorData<DATA_W, VLEN>& in_W)
{
    memory_step(s, rst, enable, in_N, in_S, in_E, in_W);
}

template <int DATA_W, int VLEN, int SIZE_WORDS>
inline void cell_program(PE_Memory_State<DATA_W, VLEN, SIZE_WORDS>& s, ap_uint<8> slot, const PE_Instruction<DATA_W>& instr)
{
    memory_program(s, slot, instr);
}

template <int DATA_W, int VLEN, int SIZE_WORDS>
inline void cell_clear_acc(PE_Memory_State<DATA_W, VLEN, SIZE_WORDS>& s)
{
    memory_clear_acc(s);
}

#endif // PE_MEMORY_HLS_C_H
