// PE_Memory_HLS_Cell.h
// Version HLS-compatible de memory/PE_Memory_Mesh_Cell.h: sin TLM (sin
// sockets, sin b_transport), sin SC_THREAD+wait(tiempo). SRAM y motor de
// rafaga viven en el mismo modulo, como arreglo fijo + FSM en un unico
// SC_METHOD sensible a clk.pos() -- mismo espiritu que pe_hls/*/PE_*_HLS.h:
// un word transferido por flanco de reloj, nunca un wait() temporizado.
//
// Convenio de campos por instr_in identico al de PE_Memory_Mesh_Cell
// (MemCellField, make_memory_field_instr): addr[1:0] selecciona contexto,
// reg_dst selecciona el campo, imm es el valor. reg_dst=MEM_FIELD_START
// (0x1F) dispara la rafaga sobre el contexto seleccionado por addr, igual
// que escribir el registro de control 0x80 de PE_Memory_Cell.
//
// Simplificaciones heredadas del original (documentadas, no bugs): SRAM
// direccionada por palabra (no por byte -- una posicion de arreglo, no un
// offset en bytes), unico puerto NoC atado al borde oeste.
//
// Cambio deliberado respecto al bridge TLM original: la escritura de campos
// es sincrona (clk.pos()), no combinacional-sobre-instr_in. El testbench ya
// hace advance_cycles(1) despues de cada load_instr, asi que el conteo de
// ciclos observable no cambia (ver plan de HLS, riesgo #5).

#ifndef PE_MEMORY_HLS_CELL_H
#define PE_MEMORY_HLS_CELL_H

#include <systemc.h>
#include "../pe/pe_isa.h"
#include "../memory/Access_controller.h"

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
inline PE_Instruction<DATA_W> make_memory_field_instr(int field, int32_t value) {
    PE_Instruction<DATA_W> instr;
    instr.reg_dst = field;
    instr.imm = value;
    return instr;
}

template <int DATA_W = 32, int VLEN = 4, int SIZE_WORDS = 512>
class PE_Memory_HLS_Cell : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_InstrIn<DATA_W>          InstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_in<Link>  in_N, in_S, in_E, in_W;
    sc_out<Link> out_N, out_S, out_E, out_W;

    sc_in<InstrIn> instr_in;

    SC_HAS_PROCESS(PE_Memory_HLS_Cell);

    explicit PE_Memory_HLS_Cell(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          instr_in("instr_in"),
          state(ST_IDLE), active_context(0), dir(0), busy_(false), done_(false)
    {
        for (int i = 0; i < SIZE_WORDS; i++) sram[i] = 0;

        SC_METHOD(tick);
        sensitive << clk.pos();
    }

    void trace(sc_core::sc_trace_file* tf) const {
        std::string p = name();
        sc_trace(tf, busy_, p + ".busy");
        sc_trace(tf, done_, p + ".done");
    }

    // Utilidades para el orquestador/testbench (mismo shape que
    // PE_Memory_Mesh_Cell::dma_busy/dma_done).
    bool dma_done() const { return done_; }
    bool dma_busy() const { return busy_; }
    sc_int<DATA_W> read_sram(int addr) const { return sram[addr % SIZE_WORDS]; }
    void write_sram(int addr, sc_int<DATA_W> value) { sram[addr % SIZE_WORDS] = value; }

private:
    enum State { ST_IDLE, ST_BURST };

    struct ContextRegisters {
        uint32_t src_addr = 0;
        uint32_t dst_addr = 0;
        int32_t  stride = 0;
        uint32_t count = 0;
        uint32_t mode = 0; // 0: Direct, 1: Stride
        uint32_t dir = 0;  // 0: SRAM->NoC(W), 1: NoC(W)->SRAM, 2: SRAM->SRAM
    };

    sc_int<DATA_W> sram[SIZE_WORDS];
    ContextRegisters contexts[4];

    State    state;
    uint32_t active_context;
    uint32_t dir;
    bool     busy_, done_;
    AccessController src_ac, dst_ac;

    void write_context_field(ContextRegisters& ctx, uint32_t field, uint32_t value) {
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

    void step_burst() {
        if (!(src_ac.has_next() && dst_ac.has_next())) {
            state = ST_IDLE;
            busy_ = false;
            done_ = true;
            return;
        }

        uint32_t s = src_ac.next_address();
        uint32_t d = dst_ac.next_address();

        if (dir == 0) {
            Link out = out_W.read();
            out[0] = sram[s % SIZE_WORDS];
            out_W.write(out);
        } else if (dir == 1) {
            sram[d % SIZE_WORDS] = in_W.read()[0];
        } else if (dir == 2) {
            sram[d % SIZE_WORDS] = sram[s % SIZE_WORDS];
        }
    }

    void tick() {
        if (rst.read()) {
            state = ST_IDLE;
            busy_ = false;
            done_ = false;
            return;
        }
        if (!enable.read()) return;

        if (state == ST_BURST) {
            step_burst();
            return;
        }

        // ST_IDLE: atender una carga de campo o un START.
        InstrIn in = instr_in.read();
        if (!in.valid) return;

        uint32_t ctx = in.addr.to_uint() & 0x3;
        uint32_t field = in.instr.reg_dst.to_uint();
        uint32_t value = static_cast<uint32_t>(in.instr.imm.to_int());

        if (field == MEM_FIELD_START) {
            active_context = ctx;
            dir = contexts[ctx].dir;
            src_ac.configure(contexts[ctx].src_addr, contexts[ctx].stride, contexts[ctx].count, contexts[ctx].mode);
            dst_ac.configure(contexts[ctx].dst_addr, contexts[ctx].stride, contexts[ctx].count, contexts[ctx].mode);
            state = ST_BURST;
            busy_ = true;
            done_ = false;
        } else {
            write_context_field(contexts[ctx], field, value);
        }
    }
};

#endif // PE_MEMORY_HLS_CELL_H
