#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <map>
#include <iostream>
#include <string>
#include "PE_Memory_Cell.h"

using namespace sc_core;
using namespace tlm;

namespace {

SC_MODULE(DummyNoC) {
public:
    tlm_utils::simple_target_socket<DummyNoC> target_socket;
    std::map<uint64_t, uint32_t> noc_mem;

    SC_CTOR(DummyNoC) : target_socket("target_socket") {
        target_socket.register_b_transport(this, &DummyNoC::b_transport);
    }

    void b_transport(tlm_generic_payload& trans, sc_time& delay) {
        uint64_t addr = trans.get_address();
        uint32_t* data = reinterpret_cast<uint32_t*>(trans.get_data_ptr());
        tlm_command cmd = trans.get_command();

        if (cmd == TLM_READ_COMMAND) {
            *data = static_cast<uint32_t>(addr + 5000);
            delay += sc_time(5, SC_NS);
            trans.set_response_status(TLM_OK_RESPONSE);
        } else if (cmd == TLM_WRITE_COMMAND) {
            noc_mem[addr] = *data;
            delay += sc_time(5, SC_NS);
            trans.set_response_status(TLM_OK_RESPONSE);
        } else {
            trans.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
        }
    }
};

SC_MODULE(ConfigHost) {
public:
    tlm_utils::simple_initiator_socket<ConfigHost> csr_socket;
    tlm_utils::simple_initiator_socket<ConfigHost> dummy_noc_init_socket;

    SC_CTOR(ConfigHost) : csr_socket("csr_socket"), dummy_noc_init_socket("dummy_noc_init_socket") {
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

    uint32_t read_csr(uint64_t addr) {
        tlm_generic_payload trans;
        sc_time delay = SC_ZERO_TIME;
        uint32_t val = 0;
        trans.set_command(TLM_READ_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&val));
        trans.set_data_length(4);
        trans.set_streaming_width(4);
        csr_socket->b_transport(trans, delay);
        wait(delay);
        return val;
    }

    void run() {
        wait(10, SC_NS);

        // Configure a direct SRAM->NoC transfer.
        write_csr(0x00, 0x0);      // src_addr = 0
        write_csr(0x04, 0x1000);   // dst_addr = 0x1000 (NoC address)
        write_csr(0x08, 0);        // stride = 0
        write_csr(0x0c, 1);        // count = 1
        write_csr(0x10, 0);        // mode = direct
        write_csr(0x14, 0);        // dir = SRAM -> NoC

        // Start the DMA engine.
        write_csr(0x80, 1);

        // Wait until done.
        uint32_t status = 0;
        do {
            wait(10, SC_NS);
            status = read_csr(0x84);
        } while ((status & 2) == 0);

        if (status == 0) {
            std::cerr << "FAIL: DMA did not report completion" << std::endl;
            sc_stop();
            return;
        }
    }
};

}  // namespace

int sc_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    PE_Memory_Cell<2048> cell("cell");
    ConfigHost host("host");
    DummyNoC dummy("dummy");

    host.csr_socket.bind(cell.csr_socket);
    host.dummy_noc_init_socket.bind(cell.noc_target_socket);
    cell.noc_initiator_socket.bind(dummy.target_socket);

    // Seed SRAM with a known value.
    cell.sram.write_word(0, 0xA5A5A5A5);

    sc_start();

    const uint32_t expected = 0xA5A5A5A5;
    const uint32_t actual = dummy.noc_mem[0x1000];

    if (actual != expected) {
        std::cerr << "FAIL: expected NoC[0x1000]=" << std::hex << expected
                  << " but got " << actual << std::dec << std::endl;
        return 1;
    }

    std::cout << "PASS: memory cell DMA transfer test" << std::endl;
    return 0;
}
