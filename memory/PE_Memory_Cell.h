#ifndef PE_MEMORY_CELL_H
#define PE_MEMORY_CELL_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include "SRAM_dual_port.h"
#include "Local_DMA.h"

using namespace sc_core;
using namespace tlm;

template <int SIZE_BYTES = 2048>
class PE_Memory_Cell : public sc_module {
public:
    // Sockets for mesh connection (NoC) and processor interface (CSR)
    tlm_utils::simple_target_socket<PE_Memory_Cell> csr_socket;
    tlm_utils::simple_target_socket<PE_Memory_Cell> noc_target_socket;
    tlm::tlm_initiator_socket<32> noc_initiator_socket;

    // Submodules
    SRAM_dual_port<SIZE_BYTES> sram;
    LocalDMA dma;

    struct ContextRegisters {
        uint32_t src_addr = 0;
        uint32_t dst_addr = 0;
        int32_t stride = 0;
        uint32_t count = 0;
        uint32_t mode = 0; // 0: Direct, 1: Stride
        uint32_t dir = 0;  // 0: SRAM to NoC, 1: NoC to SRAM, 2: SRAM to SRAM
    };

    SC_HAS_PROCESS(PE_Memory_Cell);

    explicit PE_Memory_Cell(sc_module_name name)
        : sc_module(name),
          csr_socket("csr_socket"),
          noc_target_socket("noc_target_socket"),
          noc_initiator_socket("noc_initiator_socket"),
          sram("sram"),
          dma("dma"),
          active_context(0)
    {
        // Register TLM callbacks
        csr_socket.register_b_transport(this, &PE_Memory_Cell::b_transport_csr);
        noc_target_socket.register_b_transport(this, &PE_Memory_Cell::b_transport_noc);

        // Bind DMA to SRAM ports
        dma.sram_socket_0.bind(sram.target_socket_0);
        dma.sram_socket_1.bind(sram.target_socket_1);

        // Bind DMA to external NoC output
        dma.noc_socket.bind(noc_initiator_socket);
    }

    void trace(sc_core::sc_trace_file* tf) const {
        std::string p = name();
        sc_trace(tf, active_context, p + ".active_context");
        sc_trace(tf, dma.busy, p + ".dma_busy");
        sc_trace(tf, dma.done, p + ".dma_done");
        
        for (int i = 0; i < 4; ++i) {
            std::string ctx_name = p + ".context_" + std::to_string(i);
            sc_trace(tf, contexts[i].src_addr, ctx_name + ".src_addr");
            sc_trace(tf, contexts[i].dst_addr, ctx_name + ".dst_addr");
            sc_trace(tf, contexts[i].stride, ctx_name + ".stride");
            sc_trace(tf, contexts[i].count, ctx_name + ".count");
            sc_trace(tf, contexts[i].mode, ctx_name + ".mode");
            sc_trace(tf, contexts[i].dir, ctx_name + ".dir");
        }
    }

private:
    ContextRegisters contexts[4];
    uint32_t active_context;

    // Receive data transactions from NoC and forward to SRAM
    void b_transport_noc(tlm_generic_payload& trans, sc_time& delay) {
        // Forwarding to port 0 of SRAM
        sram.transport_port_0(trans, delay);
    }

    // Handles register configuration from Risc-V core or config bridge
    void b_transport_csr(tlm_generic_payload& trans, sc_time& delay) {
        uint64_t addr = trans.get_address();
        uint32_t* data_ptr = reinterpret_cast<uint32_t*>(trans.get_data_ptr());
        tlm_command cmd = trans.get_command();

        if (cmd == TLM_WRITE_COMMAND) {
            uint32_t val = *data_ptr;
            if (addr < 0x80) {
                int ctx_idx = addr / 0x20;
                int reg_offset = addr % 0x20;
                switch (reg_offset) {
                    case 0x00: contexts[ctx_idx].src_addr = val; break;
                    case 0x04: contexts[ctx_idx].dst_addr = val; break;
                    case 0x08: contexts[ctx_idx].stride = static_cast<int32_t>(val); break;
                    case 0x0c: contexts[ctx_idx].count = val; break;
                    case 0x10: contexts[ctx_idx].mode = val; break;
                    case 0x14: contexts[ctx_idx].dir = val; break;
                    default: trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE); return;
                }
            } else if (addr == 0x80) {
                // Control register: bit 0 is START, bits 2:1 is active context
                active_context = (val >> 1) & 0x3;
                bool start = val & 0x1;
                if (start) {
                    // Copy parameters to DMA
                    dma.src_addr = contexts[active_context].src_addr;
                    dma.dst_addr = contexts[active_context].dst_addr;
                    dma.stride = contexts[active_context].stride;
                    dma.count = contexts[active_context].count;
                    dma.mode = contexts[active_context].mode;
                    dma.dir = contexts[active_context].dir;
                    dma.start_event.notify(delay);
                }
            } else {
                trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            trans.set_response_status(TLM_OK_RESPONSE);
        } else if (cmd == TLM_READ_COMMAND) {
            if (addr < 0x80) {
                int ctx_idx = addr / 0x20;
                int reg_offset = addr % 0x20;
                switch (reg_offset) {
                    case 0x00: *data_ptr = contexts[ctx_idx].src_addr; break;
                    case 0x04: *data_ptr = contexts[ctx_idx].dst_addr; break;
                    case 0x08: *data_ptr = static_cast<uint32_t>(contexts[ctx_idx].stride); break;
                    case 0x0c: *data_ptr = contexts[ctx_idx].count; break;
                    case 0x10: *data_ptr = contexts[ctx_idx].mode; break;
                    case 0x14: *data_ptr = contexts[ctx_idx].dir; break;
                    default: trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE); return;
                }
            } else if (addr == 0x80) {
                *data_ptr = (active_context << 1);
            } else if (addr == 0x84) {
                // Status register: bit 0 is BUSY, bit 1 is DONE
                *data_ptr = (dma.done ? 2 : 0) | (dma.busy ? 1 : 0);
            } else {
                trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            trans.set_response_status(TLM_OK_RESPONSE);
        } else {
            trans.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
        }
    }
};

#endif // PE_MEMORY_CELL_H
