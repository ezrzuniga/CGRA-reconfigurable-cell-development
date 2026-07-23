#ifndef LOCAL_DMA_H
#define LOCAL_DMA_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "Access_controller.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(LocalDMA) {
public:
    // Initiator sockets
    tlm_utils::simple_initiator_socket<LocalDMA> sram_socket_0;
    tlm_utils::simple_initiator_socket<LocalDMA> sram_socket_1;
    tlm_utils::simple_initiator_socket<LocalDMA> noc_socket;

    // Control signals/events
    sc_event start_event;
    
    // Config registers (written by the top-level cell)
    uint32_t src_addr;
    uint32_t dst_addr;
    int32_t stride;
    uint32_t count;
    uint32_t mode; // 0: Direct, 1: Stride
    uint32_t dir;  // 0: SRAM to NoC, 1: NoC to SRAM, 2: SRAM to SRAM

    // Status registers (read by the top-level cell)
    bool busy;
    bool done;

    SC_CTOR(LocalDMA)
        : sram_socket_0("sram_socket_0"),
          sram_socket_1("sram_socket_1"),
          noc_socket("noc_socket"),
          src_addr(0), dst_addr(0), stride(0), count(0), mode(0), dir(0),
          busy(false), done(false)
    {
        SC_THREAD(dma_thread);
    }

private:
    AccessController src_ac;
    AccessController dst_ac;

    void dma_thread() {
        while (true) {
            wait(start_event);
            busy = true;
            done = false;

            // Configure address controllers
            src_ac.configure(src_addr, stride, count, mode);
            dst_ac.configure(dst_addr, stride, count, mode);

            while (src_ac.has_next() && dst_ac.has_next()) {
                uint32_t s_addr = src_ac.next_address();
                uint32_t d_addr = dst_ac.next_address();

                if (dir == 0) {
                    // SRAM to NoC
                    uint32_t val = read_word_tlm(sram_socket_0, s_addr);
                    write_word_tlm(noc_socket, d_addr, val);
                } else if (dir == 1) {
                    // NoC to SRAM
                    uint32_t val = read_word_tlm(noc_socket, s_addr);
                    write_word_tlm(sram_socket_0, d_addr, val);
                } else if (dir == 2) {
                    // SRAM to SRAM
                    uint32_t val = read_word_tlm(sram_socket_0, s_addr);
                    write_word_tlm(sram_socket_1, d_addr, val);
                }
                
                // Simulate DMA word transfer cycle latency
                wait(10, SC_NS); 
            }

            busy = false;
            done = true;
        }
    }

    uint32_t read_word_tlm(tlm_utils::simple_initiator_socket<LocalDMA>& socket, uint64_t addr) {
        tlm_generic_payload trans;
        sc_time delay = SC_ZERO_TIME;
        uint32_t val = 0;

        trans.set_command(TLM_READ_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&val));
        trans.set_data_length(4);
        trans.set_streaming_width(4);

        socket->b_transport(trans, delay);

        if (trans.get_response_status() != TLM_OK_RESPONSE) {
            SC_REPORT_ERROR("LocalDMA", "TLM Read failed.");
        }
        wait(delay);
        return val;
    }

    void write_word_tlm(tlm_utils::simple_initiator_socket<LocalDMA>& socket, uint64_t addr, uint32_t val) {
        tlm_generic_payload trans;
        sc_time delay = SC_ZERO_TIME;

        trans.set_command(TLM_WRITE_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&val));
        trans.set_data_length(4);
        trans.set_streaming_width(4);

        socket->b_transport(trans, delay);

        if (trans.get_response_status() != TLM_OK_RESPONSE) {
            SC_REPORT_ERROR("LocalDMA", "TLM Write failed.");
        }
        wait(delay);
    }
};

#endif // LOCAL_DMA_H
