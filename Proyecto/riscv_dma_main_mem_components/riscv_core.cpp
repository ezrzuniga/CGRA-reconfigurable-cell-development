#include "riscv_core.h"
#include "cgra_kernel.h"
#include "../mesh_wrapper/mesh_wrapper.h"

#include <cstring>
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

    test_full_pipeline();

    wait(50, SC_NS);

    test_matmul();

    wait(50, SC_NS);

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
    input_data.resize(data_size);

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

static void print_vector_lanes(const std::vector<uint8_t>& bytes, const char* label)
{
    // Accept both 32 bytes (full input with A and B) and 16 bytes (single vector)
    if (bytes.size() != 32 && bytes.size() != 16) {
        return;
    }

    // For 32 bytes, print only the first 4 lanes (16 bytes = 4 * int32)
    // For 16 bytes, print all 4 lanes
    std::size_t lanes_to_print = 4;
    
    const int32_t* lanes = reinterpret_cast<const int32_t*>(bytes.data());
    std::cout << label << " [";
    for (std::size_t i = 0; i < lanes_to_print; ++i)
    {
        std::cout << lanes[i];
        if (i + 1 < lanes_to_print) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n";
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

    const uint32_t output_bytes = (cgra_config == VECTOR_ADD || cgra_config == FULL_PIPELINE || cgra_config == MATMUL) ? 16 : data_size;

    output_data.resize(output_bytes);

    trans.set_command(TLM_READ_COMMAND);
    trans.set_address(output_addr);
    trans.set_data_ptr(output_data.data());
    trans.set_data_length(output_bytes);
    trans.set_streaming_width(output_bytes);

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

    if (cgra_config == VECTOR_ADD || cgra_config == FULL_PIPELINE || cgra_config == MATMUL)
    {
        const int32_t* lanes = reinterpret_cast<const int32_t*>(output_data.data());
        const uint32_t lane_count = output_bytes / sizeof(int32_t);

        for (uint32_t i = 0; i < lane_count; ++i)
        {
            std::cout << lanes[i] << " ";
        }

        std::cout << "\n";
        std::cout << "CGRA output lanes: [";
        for (uint32_t i = 0; i < lane_count; ++i)
        {
            std::cout << lanes[i];
            if (i + 1 < lane_count) {
                std::cout << ", ";
            }
        }
        std::cout << "]\n";
    }
    else
    {
        for (uint32_t i = 0; i < output_bytes; ++i)
        {
            std::cout << static_cast<uint32_t>(output_data[i]) << " ";
        }
    }

    std::cout << "============================\n";
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
    // Input vector encoded as two 4-lane int32 vectors.
    //--------------------------------------------------

    const int32_t a[4] = {1, 2, 3, 4};
    const int32_t b[4] = {5, 6, 7, 8};
    const int32_t expected[4] = {6, 8, 10, 12};

    input_data.resize(32);
    std::memcpy(input_data.data(), a, sizeof(a));
    std::memcpy(input_data.data() + sizeof(a), b, sizeof(b));

    std::cout << "RISC-V -> bridge input (A, B):\n";
    print_vector_lanes(input_data, "  A");
    print_vector_lanes(std::vector<uint8_t>(input_data.begin() + 16, input_data.end()), "  B");

    //--------------------------------------------------
    // Number of bytes to transfer.
    //--------------------------------------------------

    data_size = input_data.size();

    //--------------------------------------------------
    // Golden reference.
    //--------------------------------------------------

    golden_reference.resize(sizeof(expected));
    std::memcpy(golden_reference.data(), expected, sizeof(expected));

    //--------------------------------------------------
    // Execute the complete CGRA flow.
    //--------------------------------------------------
    load_input_data();
    configure_cgra();
    start_cgra();
    wait_for_completion();
    read_results();

    const int32_t* got = reinterpret_cast<const int32_t*>(output_data.data());
    const int32_t* expected_vec = expected;
    bool pass = true;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (got[i] != expected_vec[i])
        {
            pass = false;
            break;
        }
    }

    std::cout << (pass ? "VECTOR ADD TEST PASSED.\n" : "VECTOR ADD TEST FAILED.\n");
}

// Ejercita el arreglo CGRA 2x2 completo (Enrutamiento, Memoria, Escalar,
// Vectorial -- ver mesh_wrapper/mesh_wrapper.h, PROGRAM_FULL_PIPELINE) desde el
// mismo flujo de software que test_vector_add(), pero con b viajando por
// Enrutamiento+Memoria antes de llegar a Escalar, y Vectorial aplicando un
// multiplicador real (no solo un passthrough): e = (a + b) * 2.
void RiscvCore::test_full_pipeline()
{
    std::cout << "\n=========================================\n";
    std::cout << "       Running FULL PIPELINE TEST\n";
    std::cout << "  (Enrutamiento -> Memoria -> Escalar -> Vectorial)\n";
    std::cout << "=========================================\n";

    //--------------------------------------------------
    // Configure the CGRA kernel.
    //--------------------------------------------------

    cgra_config = FULL_PIPELINE;

    //--------------------------------------------------
    // Main Memory addresses.
    //--------------------------------------------------

    input_addr  = 0x1000;
    output_addr = 0x2000;

    //--------------------------------------------------
    // Input vector encoded as two 4-lane int32 vectors.
    //--------------------------------------------------

    // b debe ser uniforme entre lanes (un escalar de verdad): viaja por
    // Enrutamiento->Memoria->Escalar, que solo transportan lane 0 (ver
    // mesh_wrapper/mesh_wrapper.cpp, handle_config_write). a si puede variar
    // libremente por lane: llega directo al borde real de Vectorial.
    const int32_t a[4] = {2, 4, 6, 8};
    const int32_t b[4] = {1, 1, 1, 1};
    const int32_t expected[4] = {4, 6, 8, 10};  // a + b*2

    input_data.resize(32);
    std::memcpy(input_data.data(), a, sizeof(a));
    std::memcpy(input_data.data() + sizeof(a), b, sizeof(b));

    std::cout << "RISC-V -> bridge input (A, B):\n";
    print_vector_lanes(input_data, "  A");
    print_vector_lanes(std::vector<uint8_t>(input_data.begin() + 16, input_data.end()), "  B");

    //--------------------------------------------------
    // Number of bytes to transfer.
    //--------------------------------------------------

    data_size = input_data.size();

    //--------------------------------------------------
    // Golden reference.
    //--------------------------------------------------

    golden_reference.resize(sizeof(expected));
    std::memcpy(golden_reference.data(), expected, sizeof(expected));

    //--------------------------------------------------
    // Execute the complete CGRA flow.
    //--------------------------------------------------
    load_input_data();
    configure_cgra();
    start_cgra();
    wait_for_completion();
    read_results();

    const int32_t* got = reinterpret_cast<const int32_t*>(output_data.data());
    const int32_t* expected_vec = expected;
    bool pass = true;
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (got[i] != expected_vec[i])
        {
            pass = false;
            break;
        }
    }

    std::cout << (pass ? "FULL PIPELINE TEST PASSED.\n" : "FULL PIPELINE TEST FAILED.\n");
}

// Multiplicacion matricial 2x2 (C = A * B) por el flujo completo
// RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper: el RISC-V provee A y B,
// indica el kernel MATMUL, arranca la CGRA, espera DONE, lee C desde Main Memory
// y la verifica contra un golden calculado en software. Toda la aritmetica (8
// multiplicaciones + 4 sumas) la hace la celda Vectorial de la CGRA; el RISC-V
// solo orquesta y verifica. Ver mesh_wrapper.cpp, run_matmul_dataflow.
void RiscvCore::test_matmul()
{
    std::cout << "\n=========================================\n";
    std::cout << "       Running MATMUL 2x2 TEST\n";
    std::cout << "  (C = A * B, computo integro en la celda Vectorial)\n";
    std::cout << "=========================================\n";

    cgra_config = MATMUL;

    input_addr  = 0x1000;
    output_addr = 0x2000;

    // Corre un caso 2x2 por el flujo completo y lo verifica contra el golden en
    // software. A y B son row-major: {A00,A01,A10,A11} y {B00,B01,B10,B11}.
    // Devuelve true si C (leida desde Main Memory) coincide con A*B.
    auto run_case = [this](const int32_t A[4], const int32_t B[4]) -> bool {
        int32_t expected[4];
        expected[0] = A[0] * B[0] + A[1] * B[2];  // C00 = A00*B00 + A01*B10
        expected[1] = A[0] * B[1] + A[1] * B[3];  // C01 = A00*B01 + A01*B11
        expected[2] = A[2] * B[0] + A[3] * B[2];  // C10 = A10*B00 + A11*B10
        expected[3] = A[2] * B[1] + A[3] * B[3];  // C11 = A10*B01 + A11*B11

        input_data.resize(32);
        std::memcpy(input_data.data(), A, 4 * sizeof(int32_t));
        std::memcpy(input_data.data() + 4 * sizeof(int32_t), B, 4 * sizeof(int32_t));

        std::cout << "RISC-V -> bridge input (A, B):\n";
        print_vector_lanes(input_data, "  A");
        print_vector_lanes(std::vector<uint8_t>(input_data.begin() + 16, input_data.end()), "  B");

        data_size = input_data.size();

        golden_reference.resize(sizeof(expected));
        std::memcpy(golden_reference.data(), expected, sizeof(expected));

        //----------------------------------------------
        // Flujo CGRA completo.
        //----------------------------------------------
        load_input_data();
        configure_cgra();
        start_cgra();
        wait_for_completion();
        read_results();

        const int32_t* got = reinterpret_cast<const int32_t*>(output_data.data());
        bool pass = true;
        for (uint32_t i = 0; i < 4; ++i)
        {
            if (got[i] != expected[i]) { pass = false; }
        }

        std::cout << "Expected C (row-major): [" << expected[0] << ", " << expected[1]
                  << ", " << expected[2] << ", " << expected[3] << "]\n";
        std::cout << (pass ? "  -> case OK\n" : "  -> case MISMATCH\n");
        return pass;
    };

    // Dos casos: uno simetrico y uno con matrices asimetricas / negativas, para
    // que un error de indice por-lane (transponer A o B, confundir filas con
    // columnas) no pase inadvertido.
    const int32_t A1[4] = {1, 2, 3, 4};
    const int32_t B1[4] = {5, 6, 7, 8};      // A1*B1 = {19,22,43,50}

    const int32_t A2[4] = {2, 0, 1, -3};
    const int32_t B2[4] = {1, 4, -2, 5};     // A2*B2 = {2,8,7,-11}

    bool pass = true;
    std::cout << "\n-- Caso 1: A={{1,2},{3,4}}  B={{5,6},{7,8}} --\n";
    pass &= run_case(A1, B1);
    std::cout << "\n-- Caso 2: A={{2,0},{1,-3}}  B={{1,4},{-2,5}} --\n";
    pass &= run_case(A2, B2);

    std::cout << (pass ? "MATMUL 2x2 TEST PASSED.\n" : "MATMUL 2x2 TEST FAILED.\n");
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