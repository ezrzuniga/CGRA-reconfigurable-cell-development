#include "riscv_core.h"
#include <dummy_cgra.h>

#include <iostream>

using namespace sc_core;
using namespace tlm;


//======================================================
// Constructor
//======================================================

RiscvCore::RiscvCore(sc_module_name name)
    : sc_module(name),
      csr_socket("csr_socket"),
      memory_socket("memory_socket")
{
    //--------------------------------------------------
    // Example configuration values.
    //--------------------------------------------------
    cgra_config = 0x15;
    input_addr = 0x1000;
    output_addr = 0x2000;
    data_size = 16;

    SC_THREAD(run);
}


//======================================================
// Main software program
//======================================================

void RiscvCore::run()
{
    wait(10, SC_NS);
    std::cout << "\nStarting RISC-V software...\n";
    // Flow execution

    wait(10, SC_NS);

    test_vector_add();

    wait(50, SC_NS);

    test_fir();

    wait(50, SC_NS);

    test_fft();

    std::cout << "\nRISC-V program finished.\n";
}


//======================================================
// Write the input data into Main Memory.
//======================================================
void RiscvCore::load_input_data()
{
    sc_time delay = SC_ZERO_TIME;
    tlm_generic_payload trans;

    //--------------------------------------------------
    // input data.
    //--------------------------------------------------
    std::vector<uint8_t> input_data(data_size);
    // Collect input data
    for(uint32_t i = 0; i < data_size; ++i)
    {
        input_data[i] = static_cast<uint8_t>(i);
    }

    trans.set_command(TLM_WRITE_COMMAND);
    trans.set_address(input_addr);
    trans.set_data_ptr(input_data.data());
    trans.set_data_length(data_size);
    trans.set_streaming_width(data_size);

    memory_socket->b_transport(trans, delay);

    if(trans.get_response_status() != TLM_OK_RESPONSE)
    {
        SC_REPORT_ERROR(
            "RISCV_CORE",
            "Failed to write the input data into Main Memory.");
    }

    std::cout << "Input data loaded into Main Memory.\n";
}

//======================================================
// Configure the CSR registers.
//======================================================

void RiscvCore::configure_cgra()
{
    sc_time delay = SC_ZERO_TIME;
    tlm_generic_payload trans;
    uint32_t value;

    //--------------------------------------------------
    // CGRA Configuration Register
    //--------------------------------------------------
    value = cgra_config;

    trans.set_command(TLM_WRITE_COMMAND);
    trans.set_address(0x00);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
    trans.set_data_length(sizeof(uint32_t));
    trans.set_streaming_width(sizeof(uint32_t));

    csr_socket->b_transport(trans, delay);

    //--------------------------------------------------
    // Input Address Register
    //--------------------------------------------------
    value = input_addr;

    trans.set_address(0x04);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    csr_socket->b_transport(trans, delay);

    //--------------------------------------------------
    // Output Address Register
    //--------------------------------------------------
    value = output_addr;

    trans.set_address(0x08);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));

    csr_socket->b_transport(trans, delay);

    //--------------------------------------------------
    // Data Size Register
    //--------------------------------------------------
    value = data_size;

    trans.set_address(0x0C);
    trans.set_data_ptr(
        reinterpret_cast<unsigned char*>(&value));

    csr_socket->b_transport(trans, delay);


    std::cout << "CGRA successfully configured.\n";
}

//======================================================
// Start the CGRA
//======================================================
void RiscvCore::start_cgra()
{
    sc_time delay = SC_ZERO_TIME;
    tlm_generic_payload trans;
    uint32_t start_signal = 1;

    trans.set_command(TLM_WRITE_COMMAND);
    trans.set_address(0x10);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&start_signal));
    trans.set_data_length(sizeof(uint32_t));
    trans.set_streaming_width(sizeof(uint32_t));

    csr_socket->b_transport(trans, delay);

    if(trans.get_response_status() != TLM_OK_RESPONSE)
    {
        SC_REPORT_ERROR(
            "RISCV_CORE",
            "Failed to start the CGRA.");
    }

    std::cout << "CGRA execution started.\n";
}

//======================================================
// Monitoring the DONE register once the CGRA finish operation
//======================================================
void RiscvCore::wait_for_completion()
{
    sc_time delay = SC_ZERO_TIME;
    uint32_t done = 0;

    while(!done)
    {
        tlm_generic_payload trans;
        trans.set_command(TLM_READ_COMMAND);
        trans.set_address(0x18);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&done));
        trans.set_data_length(sizeof(uint32_t));
        trans.set_streaming_width(sizeof(uint32_t));

        csr_socket->b_transport(trans, delay);

        wait(10, SC_NS);
    }

    std::cout << "CGRA execution completed.\n";
}

//======================================================
// Read the output buffer from Main Memory.
//======================================================
void RiscvCore::read_results()
{
    sc_time delay = SC_ZERO_TIME;
    tlm_generic_payload trans;

    output_data.resize(data_size);

    trans.set_command(TLM_READ_COMMAND);
    trans.set_address(output_addr);
    trans.set_data_ptr(output_data.data());
    trans.set_data_length(data_size);
    trans.set_streaming_width(data_size);

    memory_socket->b_transport(trans, delay);

    if(trans.get_response_status() != TLM_OK_RESPONSE)
    {
        SC_REPORT_ERROR(
            "RISCV_CORE",
            "Failed to read the output buffer.");
    }

    //--------------------------------------------------
    // Display the results.
    //--------------------------------------------------
    std::cout << "\n===== CGRA OUTPUT DATA =====\n";
    for(uint32_t i = 0; i < data_size; ++i)
    {
        std::cout<< static_cast<uint32_t>(output_data[i])<< " ";
    }

    std::cout<< "\n============================\n";
}

void RiscvCore::test_vector_add()
{
    std::cout << "\n=========================================\n";
    std::cout << "       Running VECTOR ADD TEST\n";
    std::cout << "=========================================\n";

    //--------------------------------------------------
    // Configure the CGRA kernel.
    //--------------------------------------------------

    cgra_config = VECTOR_ADD;

    //--------------------------------------------------
    // Main Memory addresses.
    //--------------------------------------------------

    input_addr  = 0x1000;
    output_addr = 0x2000;

    //--------------------------------------------------
    // Input vector.
    //--------------------------------------------------

    input_data =
    {
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8
    };

    //--------------------------------------------------
    // Number of bytes to transfer.
    //--------------------------------------------------

    data_size = input_data.size();

    //--------------------------------------------------
    // Golden reference.
    //--------------------------------------------------

    golden_reference =
    {
        2,
        4,
        6,
        8,
        10,
        12,
        14,
        16
    };

    //--------------------------------------------------
    // Execute the complete CGRA flow.
    //--------------------------------------------------
    load_input_data();
    configure_cgra();
    start_cgra();
    wait_for_completion();
    read_results();

    std::cout << "\nVECTOR ADD TEST FINISHED.\n";
}

void RiscvCore::test_fir()
{
    std::cout << "\n=============================\n";
    std::cout << "Running FIR FILTER TEST\n";
    std::cout << "=============================\n";


    cgra_config = FIR_FILTER;

    input_addr  = 0x1000;
    output_addr = 0x2000;
    data_size   = 8;


    //--------------------------------------------------
    // Input vector.
    //--------------------------------------------------
    input_data.resize(data_size);
    input_data =
    {
        1,2,3,4,5,6,7,8
    };


    //--------------------------------------------------
    // Golden reference.
    //--------------------------------------------------

    golden_reference.resize(data_size);
    golden_reference =
    {
        1,
        4,
        8,
        12,
        16,
        20,
        24,
        28
    };


    load_input_data();
    configure_cgra();
    start_cgra();
    wait_for_completion();
    read_results();
}

void RiscvCore::test_fft()
{
    cgra_config = FFT_8_POINTS;

    load_input_data();
    configure_cgra();
    start_cgra();
    wait_for_completion();
    read_results();
}