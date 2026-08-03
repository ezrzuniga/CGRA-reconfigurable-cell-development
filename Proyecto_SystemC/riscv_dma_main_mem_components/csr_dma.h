#ifndef CSR_DMA_H
#define CSR_DMA_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <cstdint>
#include <vector>

using namespace sc_core;
using namespace tlm;

SC_MODULE(CSR_DMA) {
public:
    //-----------------------------------------
    // TLM sockets
    //-----------------------------------------
    // Receives requests from the CPU
    tlm_utils::simple_target_socket<CSR_DMA> target_socket;
    // Accesses main memory
    tlm_utils::simple_initiator_socket<CSR_DMA> memory_socket;
    // Accesses the CGRA
    tlm_utils::simple_initiator_socket<CSR_DMA> cgra_socket;

    // Constructor
    SC_CTOR(CSR_DMA);

    //-----------------------------------------
    // TLM callback
    //-----------------------------------------

    void b_transport(tlm_generic_payload& trans, sc_time& delay);

    //-----------------------------------------
    // DMA Thread
    //-----------------------------------------

    void dma_controller();

private:
    //-----------------------------------------
    // CSR Registers
    //-----------------------------------------

    uint32_t cgra_config;
    uint32_t input_addr;
    uint32_t output_addr;
    uint32_t data_size;
    uint32_t start;
    uint32_t status;
    uint32_t done;

    //-----------------------------------------
    // DMA synchronization
    //-----------------------------------------

    sc_event start_dma_event;

    //-----------------------------------------
    // DMA buffers
    //-----------------------------------------

    std::vector<uint8_t> input_buffer;
    std::vector<uint8_t> output_buffer;

    //-----------------------------------------
    // Helper functions
    //-----------------------------------------

    void read_from_memory();

    void send_configuration();

    void send_input_data_to_cgra();

    void execute_cgra();

    void receive_output_data_from_cgra();

    void write_results();

};
#endif // CSR_DMA_H