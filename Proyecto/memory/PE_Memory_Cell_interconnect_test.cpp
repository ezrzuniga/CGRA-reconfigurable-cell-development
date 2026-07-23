#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <iostream>
#include "PE_Memory_Cell.h"
#include "Simple_NOC_Interconnect.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(ConfigHost) {
public:
    tlm_utils::simple_initiator_socket<ConfigHost> csr_socket;

    SC_CTOR(ConfigHost) : csr_socket("csr_socket") {
        SC_THREAD(run);
    }

    void write_csr(uint64_t addr, uint32_t val) {
        tlm_generic_payload trans;
        sc_time delay = SC_ZERO_TIME;
        trans.set_command(TLM_WRITE_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&val));
        trans.set_data_length(4);
        trans.set_streaming_width(4);
        csr_socket->b_transport(trans, delay);
        wait(delay);
    }

    void run() {
        wait(10, SC_NS);

        // Configure tile 0 to transfer one word from its own SRAM to tile 1 via the interconnect.
        write_csr(0x00, 0x0);      // src_addr = 0
        write_csr(0x04, 0x100);    // dst_addr = 0x100 (tile 1 SRAM offset)
        write_csr(0x08, 0);        // stride = 0
        write_csr(0x0c, 1);        // count = 1
        write_csr(0x10, 0);        // mode = direct
        write_csr(0x14, 0);        // dir = SRAM -> NoC

        write_csr(0x80, 1);        // start DMA
    }
};

SC_MODULE(NullCsrInitiator) {
public:
    tlm_utils::simple_initiator_socket<NullCsrInitiator> csr_socket;

    SC_CTOR(NullCsrInitiator) : csr_socket("csr_socket") {}
};

int sc_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    PE_Memory_Cell<2048> tile0("tile0");
    PE_Memory_Cell<2048> tile1("tile1");
    SimpleNoCInterconnect noc("noc");
    ConfigHost host("host");
    NullCsrInitiator null_csr("null_csr");

    host.csr_socket.bind(tile0.csr_socket);
    null_csr.csr_socket.bind(tile1.csr_socket);

    // Connect each tile's NoC initiator to the interconnect target port.
    tile0.noc_initiator_socket.bind(noc.target_sockets[0]);
    tile1.noc_initiator_socket.bind(noc.target_sockets[1]);

    // Connect the interconnect to each tile's target-side NoC interface.
    noc.initiator_sockets[0].bind(tile0.noc_target_socket);
    noc.initiator_sockets[1].bind(tile1.noc_target_socket);

    // Seed the source tile's SRAM.
    tile0.sram.write_word(0, 0xDEADBEEF);

    sc_start();

    const uint32_t actual = tile1.sram.read_word(0x100 / 4);
    if (actual != 0xDEADBEEF) {
        std::cerr << "FAIL: expected interconnect transfer to reach tile1, got 0x"
                  << std::hex << actual << std::dec << std::endl;
        return 1;
    }

    std::cout << "PASS: memory-cell interconnect test" << std::endl;
    return 0;
}
