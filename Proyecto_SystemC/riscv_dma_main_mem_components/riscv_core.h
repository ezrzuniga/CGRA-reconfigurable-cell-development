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

    // Veredicto agregado de la suite que corre run(): true solo si TODOS los tests
    // pasaron. Existe para que sc_main pueda devolver un exit code honesto -- antes
    // los testbenches que instancian RiscvCore imprimian "PASS" incondicionalmente y
    // el veredicto real habia que buscarlo a ojo en el stdout.
    bool all_passed() const { return all_passed_; }


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
    void test_matmul();
    void test_exp_vec();
    void test_softmax();
    void test_fir();
    void test_fft();

    // Registra el resultado de un test y lo acumula en all_passed_.
    void report_test(const char* name, bool pass);

    bool all_passed_;


    //---------------------------------------------------
    // Software configuration parameters
    //---------------------------------------------------

    uint32_t cgra_config;

    uint32_t input_addr;

    uint32_t output_addr;

    uint32_t data_size;

};

#endif