#ifndef SRAM_DUAL_PORT_H
#define SRAM_DUAL_PORT_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <vector>
#include <string>
#include <cstring>

using namespace sc_core;
using namespace tlm;

template <int SIZE_BYTES = 2048>
class SRAM_dual_port : public sc_module {
public:
    static_assert(SIZE_BYTES % 4 == 0, "SRAM size must be a multiple of 4 bytes (32-bit words)");
    static const int WORDS_COUNT = SIZE_BYTES / 4;

    // Dual ports
    tlm_utils::simple_target_socket<SRAM_dual_port> target_socket_0;
    tlm_utils::simple_target_socket<SRAM_dual_port> target_socket_1;

    SC_HAS_PROCESS(SRAM_dual_port);

    explicit SRAM_dual_port(sc_module_name name)
        : sc_module(name),
          target_socket_0("target_socket_0"),
          target_socket_1("target_socket_1"),
          mem(WORDS_COUNT, 0)
    {
        target_socket_0.register_b_transport(this, &SRAM_dual_port::b_transport_0);
        target_socket_1.register_b_transport(this, &SRAM_dual_port::b_transport_1);
    }

    // Direct access to internal storage for TB and trace
    uint32_t read_word(int word_addr) const {
        if (word_addr >= 0 && word_addr < WORDS_COUNT) {
            return mem[word_addr];
        }
        return 0;
    }

    void write_word(int word_addr, uint32_t val) {
        if (word_addr >= 0 && word_addr < WORDS_COUNT) {
            mem[word_addr] = val;
        }
    }

    void transport_port_0(tlm_generic_payload& trans, sc_time& delay) {
        handle_transaction(0, trans, delay);
    }

    void transport_port_1(tlm_generic_payload& trans, sc_time& delay) {
        handle_transaction(1, trans, delay);
    }

private:
    std::vector<uint32_t> mem;

    void b_transport_0(tlm_generic_payload& trans, sc_time& delay) {
        handle_transaction(0, trans, delay);
    }

    void b_transport_1(tlm_generic_payload& trans, sc_time& delay) {
        handle_transaction(1, trans, delay);
    }

    void handle_transaction(int port_id, tlm_generic_payload& trans, sc_time& delay) {
        uint64_t addr = trans.get_address();
        unsigned char* data = trans.get_data_ptr();
        unsigned int len = trans.get_data_length();
        tlm_command cmd = trans.get_command();

        // Check alignment and bounds
        if (addr + len > SIZE_BYTES) {
            trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        // We assume access is 32-bit aligned for simplicity, but handle byte-granular operations
        if (cmd == TLM_READ_COMMAND) {
            std::memcpy(data, reinterpret_cast<unsigned char*>(mem.data()) + addr, len);
            delay += sc_time(5, SC_NS); // 1 cycle read latency (assuming 200MHz clock)
            trans.set_response_status(TLM_OK_RESPONSE);
        } else if (cmd == TLM_WRITE_COMMAND) {
            std::memcpy(reinterpret_cast<unsigned char*>(mem.data()) + addr, data, len);
            delay += sc_time(5, SC_NS); // 1 cycle write latency
            trans.set_response_status(TLM_OK_RESPONSE);
        } else {
            trans.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
        }
    }
};

#endif // SRAM_DUAL_PORT_H
