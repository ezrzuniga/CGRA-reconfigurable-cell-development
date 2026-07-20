#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <map>
#include "PE_Memory_Cell.h"

using namespace sc_core;
using namespace tlm;

// Dummy NoC module acting as a target for DMA initiator
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
            *data = addr + 5000;
            delay += sc_time(5, SC_NS);
            trans.set_response_status(TLM_OK_RESPONSE);
        } else if (cmd == TLM_WRITE_COMMAND) {
            noc_mem[addr] = *data;
            delay += sc_time(5, SC_NS);
            trans.set_response_status(TLM_OK_RESPONSE);
        }
    }
};

// Host initiator to program registers
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

        cout << "@" << sc_time_stamp() << " [Host] Programming Context 0 (SRAM -> NoC, Stride)" << endl;
        // Programming Context 0: SRAM -> NoC
        write_csr(0x00, 0x0);   // src_addr = 0
        write_csr(0x04, 0x1000); // dst_addr = 0x1000 (NoC address)
        write_csr(0x08, 4);      // stride = 4 (words are 4-byte spaced)
        write_csr(0x0c, 5);      // count = 5
        write_csr(0x10, 1);      // mode = 1 (Stride)
        write_csr(0x14, 0);      // dir = 0 (SRAM to NoC)

        // Verify configuration reads
        if (read_csr(0x00) != 0 || read_csr(0x04) != 0x1000 || read_csr(0x08) != 4 ||
            read_csr(0x0c) != 5 || read_csr(0x10) != 1 || read_csr(0x14) != 0) {
            cout << "FAIL: Config register verification failed" << endl;
            exit(1);
        }
        cout << "@" << sc_time_stamp() << " [Host] Config registers verified successfully" << endl;

        // Trigger START with active context 0
        // val = (active_context << 1) | start_bit = (0 << 1) | 1 = 1
        write_csr(0x80, 1);

        // Poll status until done
        uint32_t status = 0;
        do {
            wait(10, SC_NS);
            status = read_csr(0x84);
            cout << "@" << sc_time_stamp() << " [Host] Polling Status: " << status << endl;
        } while ((status & 2) == 0); // Done bit is bit 1

        cout << "@" << sc_time_stamp() << " [Host] DMA Context 0 Completed!" << endl;

        // Perform Context 1 test: SRAM -> SRAM (Local Copy)
        cout << "@" << sc_time_stamp() << " [Host] Programming Context 1 (SRAM -> SRAM, Stride)" << endl;
        write_csr(0x20, 0x0);   // src_addr = 0
        write_csr(0x24, 0x100);  // dst_addr = 0x100 (SRAM target address)
        write_csr(0x28, 4);      // stride = 4
        write_csr(0x2c, 5);      // count = 5
        write_csr(0x30, 1);      // mode = 1 (Stride)
        write_csr(0x34, 2);      // dir = 2 (SRAM to SRAM)

        // Trigger Context 1 (val = (1 << 1) | 1 = 3)
        write_csr(0x80, 3);

        do {
            wait(10, SC_NS);
            status = read_csr(0x84);
        } while ((status & 2) == 0);

        cout << "@" << sc_time_stamp() << " [Host] DMA Context 1 Completed!" << endl;

        // Perform Context 2 test: NoC -> SRAM (read pattern from NoC and write to SRAM)
        cout << "@" << sc_time_stamp() << " [Host] Programming Context 2 (NoC -> SRAM, Stride)" << endl;
        write_csr(0x40, 0x200);  // src_addr = 0x200 (NoC address)
        write_csr(0x44, 0x300);  // dst_addr = 0x300 (SRAM address)
        write_csr(0x48, 4);      // stride = 4
        write_csr(0x4c, 5);      // count = 5
        write_csr(0x50, 1);      // mode = 1 (Stride)
        write_csr(0x54, 1);      // dir = 1 (NoC to SRAM)

        // Trigger Context 2 (val = (2 << 1) | 1 = 5)
        write_csr(0x80, 5);

        do {
            wait(10, SC_NS);
            status = read_csr(0x84);
        } while ((status & 2) == 0);

        cout << "@" << sc_time_stamp() << " [Host] DMA Context 2 Completed!" << endl;
    }
};

int sc_main(int argc, char* argv[]) {
    PE_Memory_Cell<2048> cell("cell");
    ConfigHost host("host");
    DummyNoC dummy("dummy");

    // Connect Host to PE_Memory_Cell CSR socket
    host.csr_socket.bind(cell.csr_socket);

    // Connect dummy NoC initiator to PE_Memory_Cell NoC target socket
    host.dummy_noc_init_socket.bind(cell.noc_target_socket);

    // Connect PE_Memory_Cell NoC initiator socket to DummyNoC target socket
    cell.noc_initiator_socket.bind(dummy.target_socket);

    // Initialize SRAM memory before start of simulation
    for (int i = 0; i < 16; ++i) {
        cell.sram.write_word(i, 42 + i);
    }
    
    // Setup tracing
    sc_trace_file* tf = sc_create_vcd_trace_file("pe_memory_cell_wave");
    cell.trace(tf);

    sc_start();

    // Verification check for Context 0 (SRAM -> NoC):
    // SRAM locations 0, 4, 8, 12, 16 contained 42, 43, 44, 45, 46.
    // They should have been written to NoC addresses 0x1000, 0x1004, 0x1008, 0x100c, 0x1010.
    bool pass = true;
    for (int i = 0; i < 5; ++i) {
        uint64_t noc_addr = 0x1000 + i * 4;
        if (dummy.noc_mem[noc_addr] != 42 + i) {
            cout << "FAIL Context 0: Expected NoC[" << hex << noc_addr << "] = " << dec << 42 + i 
                 << ", got " << dummy.noc_mem[noc_addr] << endl;
            pass = false;
        }
    }

    // Verification check for Context 1 (SRAM -> SRAM):
    // SRAM locations 0, 4, 8, 12, 16 contained 42, 43, 44, 45, 46.
    // They should have been copied to SRAM offset 0x100 (which is word address 0x100 / 4 = 64).
    for (int i = 0; i < 5; ++i) {
        uint32_t val = cell.sram.read_word(0x100/4 + i);
        if (val != 42 + i) {
            cout << "FAIL Context 1: Expected SRAM[" << dec << 0x100/4 + i << "] = " << 42 + i
                 << ", got " << val << endl;
            pass = false;
        }
    }

    // Verification check for Context 2 (NoC -> SRAM):
    // Read from NoC at 0x200, 0x204, 0x208, 0x20c, 0x210.
    // DummyNoC returns addr + 5000. So we expect:
    // NoC[0x200] = 0x200 + 5000 = 512 + 5000 = 5512.
    // These should be written to SRAM offset 0x300 (word address 0x300 / 4 = 192).
    for (int i = 0; i < 5; ++i) {
        uint32_t val = cell.sram.read_word(0x300/4 + i);
        uint32_t expected = (0x200 + i * 4) + 5000;
        if (val != expected) {
            cout << "FAIL Context 2: Expected SRAM[" << dec << 0x300/4 + i << "] = " << expected
                 << ", got " << val << endl;
            pass = false;
        }
    }

    sc_close_vcd_trace_file(tf);

    if (pass) {
        cout << "PE MEMORY CELL INTEGRATION TB PASS!" << endl;
        return 0;
    } else {
        cout << "PE MEMORY CELL INTEGRATION TB FAIL!" << endl;
        return 1;
    }
}
