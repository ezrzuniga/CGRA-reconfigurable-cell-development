#ifndef RISCV_CORE_H
#define RISCV_CORE_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <vector>
#include <cstdint>

using namespace sc_core;
using namespace tlm;

SC_MODULE(RiscvCore)
{
public:

    //---------------------------------------------------
    // TLM Sockets
    //---------------------------------------------------

    // Used to access the CSR/DMA bridge.
    tlm_utils::simple_initiator_socket<RiscvCore> csr_socket;

    // Used to access Main Memory in a direct way
    tlm_utils::simple_initiator_socket<RiscvCore> memory_socket;


    //---------------------------------------------------
    // Constructor
    //---------------------------------------------------

    SC_CTOR(RiscvCore);


private:

    // Input data to be processed by the CGRA
    std::vector<uint8_t> input_data;

    // Stores the results produced by the CGRA
    std::vector<uint8_t> output_data;

    // Stores the expected results for the current test
    std::vector<uint8_t> golden_reference;

    //---------------------------------------------------
    // Software program
    //---------------------------------------------------

    void run();


    //---------------------------------------------------
    // Software functions
    //---------------------------------------------------

    void load_input_data();

    void configure_cgra();

    void start_cgra();

    void wait_for_completion();

    void read_results();

    // Test functions for each kernel
    void test_vector_add();
    void test_full_pipeline();
    void test_fir();
    void test_fft();


    //---------------------------------------------------
    // Software configuration parameters
    //---------------------------------------------------

    uint32_t cgra_config;

    uint32_t input_addr;

    uint32_t output_addr;

    uint32_t data_size;

};

#endif