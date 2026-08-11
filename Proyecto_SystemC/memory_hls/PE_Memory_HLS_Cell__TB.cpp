// PE_Memory_HLS_Cell__TB.cpp
// Smoke test standalone de PE_Memory_HLS_Cell: escritura de contexto via
// instr_in, START, y las dos direcciones de rafaga por el borde oeste
// (NoC->SRAM y SRAM->NoC), sin ningun socket TLM en el testbench -- todo el
// estimulo son sc_signal planas y polling de dma_done().

#include <systemc.h>
#include "PE_Memory_HLS_Cell.h"
#include "../pe_hls/test_util.h"

typedef PE_VectorData<32, 4> Link;

static PE_InstrIn<32> field_instr(int addr_ctx, int field, int32_t value) {
    PE_InstrIn<32> in;
    in.valid = true;
    in.addr = addr_ctx;
    in.instr = make_memory_field_instr<32>(field, value);
    return in;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N, in_S, in_E, in_W;
    sc_signal<Link> out_N, out_S, out_E, out_W;
    sc_signal<PE_InstrIn<32>> instr_in;

    PE_Memory_HLS_Cell<32, 4, 512> mem("mem");
    mem.clk(clk); mem.rst(rst); mem.enable(enable);
    mem.in_N(in_N); mem.in_S(in_S); mem.in_E(in_E); mem.in_W(in_W);
    mem.out_N(out_N); mem.out_S(out_S); mem.out_E(out_E); mem.out_W(out_W);
    mem.instr_in(instr_in);

    bool ok = true;

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    in_W.write(Link());
    instr_in.write(PE_InstrIn<32>());
    advance_cycles(2);
    rst.write(false);
    enable.write(true);

    test_section("Contexto 0: NoC(W)->SRAM (dir=1), copia 1 word a sram[0]");
    instr_in.write(field_instr(0, MEM_FIELD_DIR, 1));      advance_cycles(1);
    instr_in.write(field_instr(0, MEM_FIELD_MODE, 0));     advance_cycles(1);
    instr_in.write(field_instr(0, MEM_FIELD_COUNT, 1));    advance_cycles(1);
    instr_in.write(field_instr(0, MEM_FIELD_SRC_ADDR, 0)); advance_cycles(1);
    instr_in.write(field_instr(0, MEM_FIELD_DST_ADDR, 0)); advance_cycles(1);
    instr_in.write(PE_InstrIn<32>());

    test_section("Estimulo: borde W = 100 (lane 0)");
    in_W.write(Link({100, 0, 0, 0}));
    advance_cycles(2);

    test_section("Contexto 0: START");
    instr_in.write(field_instr(0, MEM_FIELD_START, 1));
    advance_cycles(1);
    instr_in.write(PE_InstrIn<32>());

    bool dma1_done = false;
    for (int i = 0; i < 10 && !dma1_done; i++) {
        advance_cycles(1);
        dma1_done = mem.dma_done();
    }
    test_check_bool(ok, "DMA NoC->SRAM completo", "in_W=100", dma1_done);
    test_check(ok, "sram[0] == 100", "in_W=100", (int32_t)100, mem.read_sram(0).to_int());

    test_section("Contexto 1: SRAM->NoC(W) (dir=0), reenvia sram[0]");
    instr_in.write(field_instr(1, MEM_FIELD_DIR, 0));      advance_cycles(1);
    instr_in.write(field_instr(1, MEM_FIELD_MODE, 0));     advance_cycles(1);
    instr_in.write(field_instr(1, MEM_FIELD_COUNT, 1));    advance_cycles(1);
    instr_in.write(field_instr(1, MEM_FIELD_SRC_ADDR, 0)); advance_cycles(1);
    instr_in.write(field_instr(1, MEM_FIELD_DST_ADDR, 0)); advance_cycles(1);
    instr_in.write(field_instr(1, MEM_FIELD_START, 1));    advance_cycles(1);
    instr_in.write(PE_InstrIn<32>());

    bool dma2_done = false;
    for (int i = 0; i < 10 && !dma2_done; i++) {
        advance_cycles(1);
        dma2_done = mem.dma_done();
    }
    test_check_bool(ok, "DMA SRAM->NoC completo", "sram[0]=100", dma2_done);

    Link expected({100, 0, 0, 0});
    test_check(ok, "out_W lane0 == 100 (reenviado por SRAM->NoC)", "sram[0]=100", expected, out_W.read());

    if (ok) {
        cout << "\nPASS: PE_Memory_HLS_Cell round trip NoC->SRAM->NoC sin TLM." << endl;
    }
    return ok ? 0 : 1;
}
