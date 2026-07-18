#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "riscv_core.h"
#include "csr_dma.h"
#include "main_memory.h"

using namespace sc_core;
using namespace tlm;
using namespace std;

//----------------------------------------------------------------------
// CGRA_Stub: minimal testbench-only responder for CSR_DMA's cgra_socket.
// Implements the CGRA register map assumed in csr_dma.cpp. Not a real
// design component -- replace with the real CGRA module once it exists.
//----------------------------------------------------------------------
SC_MODULE(CGRA_Stub) {
public:
    tlm_utils::simple_target_socket<CGRA_Stub> target_socket;

    SC_CTOR(CGRA_Stub) : target_socket("target_socket"), config(0), done(false) {
        target_socket.register_b_transport(this, &CGRA_Stub::b_transport);
    }

    void b_transport(tlm_generic_payload& trans, sc_time& delay) {
        uint64_t addr = trans.get_address();
        tlm_command cmd = trans.get_command();

        if (cmd == TLM_WRITE_COMMAND) {
            switch (addr) {
                case 0x00: {
                    config = *reinterpret_cast<uint32_t*>(trans.get_data_ptr());
                    break;
                }
                case 0x04: {
                    output_buffer.resize(input_buffer.size());
                    for (size_t i = 0; i < input_buffer.size(); ++i) {
                        output_buffer[i] = static_cast<uint8_t>(input_buffer[i] + 1);
                    }
                    done = true;
                    break;
                }
                case 0x10: {
                    unsigned int len = trans.get_data_length();
                    input_buffer.resize(len);
                    memcpy(input_buffer.data(), trans.get_data_ptr(), len);
                    break;
                }
            }
        } else if (cmd == TLM_READ_COMMAND) {
            switch (addr) {
                case 0x0C: {
                    *reinterpret_cast<uint32_t*>(trans.get_data_ptr()) = done ? 1 : 0;
                    break;
                }
                case 0x14: {
                    unsigned int len = trans.get_data_length();
                    output_buffer.resize(len);
                    memcpy(trans.get_data_ptr(), output_buffer.data(), len);
                    break;
                }
            }
        }

        trans.set_response_status(TLM_OK_RESPONSE);
    }

    uint32_t get_config() const { return config; }
    bool get_done() const { return done; }
    const vector<uint8_t>& get_input_buffer() const { return input_buffer; }

private:
    uint32_t config;
    bool done;
    vector<uint8_t> input_buffer;
    vector<uint8_t> output_buffer;
};

//----------------------------------------------------------------------
// MemoryRouter: testbench-only fan-in adapter. MainMemory::socket is a
// simple_target_socket (point-to-point, single bind), but both RiscvCore
// and CSR_DMA need direct access to it -- so this just forwards both
// target sockets onto the one initiator socket bound to MainMemory.
//----------------------------------------------------------------------
SC_MODULE(MemoryRouter) {
public:
    tlm_utils::simple_target_socket<MemoryRouter> cpu_target_socket;
    tlm_utils::simple_target_socket<MemoryRouter> dma_target_socket;
    tlm_utils::simple_initiator_socket<MemoryRouter> mem_socket;

    SC_CTOR(MemoryRouter)
        : cpu_target_socket("cpu_target_socket"),
          dma_target_socket("dma_target_socket"),
          mem_socket("mem_socket") {
        cpu_target_socket.register_b_transport(this, &MemoryRouter::b_transport);
        dma_target_socket.register_b_transport(this, &MemoryRouter::b_transport);
    }

    void b_transport(tlm_generic_payload& trans, sc_time& delay) {
        mem_socket->b_transport(trans, delay);
    }
};

int sc_main(int argc, char* argv[]) {
    MainMemory mem("mem");
    MemoryRouter mem_router("mem_router");
    CSR_DMA dma("dma");
    RiscvCore cpu("cpu");
    CGRA_Stub cgra("cgra_stub");

    cpu.csr_socket.bind(dma.target_socket);
    cpu.memory_socket.bind(mem_router.cpu_target_socket);
    dma.memory_socket.bind(mem_router.dma_target_socket);
    mem_router.mem_socket.bind(mem.socket);
    dma.cgra_socket.bind(cgra.target_socket);

    sc_start(5, SC_US);

    bool pass = true;

    if (!cgra.get_done()) {
        cout << "FAIL: CGRA stub never completed (pipeline stalled).\n";
        pass = false;
    }

    if (cgra.get_config() != 0x15) {
        cout << "FAIL: CGRA config mismatch, got 0x" << hex << cgra.get_config() << dec << "\n";
        pass = false;
    }

    const vector<uint8_t>& received = cgra.get_input_buffer();
    if (received.size() != 16) {
        cout << "FAIL: CGRA input buffer size mismatch, got " << received.size() << "\n";
        pass = false;
    } else {
        for (uint32_t i = 0; i < 16; ++i) {
            if (received[i] != i) {
                cout << "FAIL: CGRA input buffer mismatch at index " << i
                     << ", got " << static_cast<uint32_t>(received[i]) << "\n";
                pass = false;
                break;
            }
        }
    }

    if (!pass) {
        return 1;
    }

    cout << "PASS: RiscvCore -> CSR_DMA -> MainMemory -> CGRA_Stub smoke test.\n";
    return 0;
}
