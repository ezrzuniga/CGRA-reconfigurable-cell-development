#ifndef DUMMY_CGRA_H
#define DUMMY_CGRA_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

#include <vector>
#include <cstdint>

using namespace sc_core;
using namespace tlm;


//======================================================
// Supported kernels
//======================================================

enum CGRA_KERNEL
{
    VECTOR_ADD = 0,
    FIR_FILTER = 1,
    FFT_8_POINTS = 2
};


//======================================================
// Dummy CGRA
//======================================================

SC_MODULE(DummyCGRA)
{
public:

    //--------------------------------------------------
    // TLM target socket
    //--------------------------------------------------

    tlm_utils::simple_target_socket<DummyCGRA> socket;


    //--------------------------------------------------
    // Constructor
    //--------------------------------------------------

    SC_CTOR(DummyCGRA);


private:

    //--------------------------------------------------
    // Registers
    //--------------------------------------------------

    uint32_t configuration;
    uint32_t start;
    uint32_t status;
    uint32_t done;


    //--------------------------------------------------
    // Internal buffers
    //--------------------------------------------------

    std::vector<uint8_t> input_buffer;
    std::vector<uint8_t> output_buffer;


    //--------------------------------------------------
    // TLM transport function
    //--------------------------------------------------

    void b_transport(tlm_generic_payload& trans,
                     sc_time& delay);


    //--------------------------------------------------
    // Kernel selection
    //--------------------------------------------------

    void compute();


    //--------------------------------------------------
    // Supported kernels
    //--------------------------------------------------

    void vector_add();

    void fir_filter();

    void fft();

};

#endif