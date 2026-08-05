#include "riscv_core.h"
#include "cgra_kernel.h"
#include "../mesh_wrapper/mesh_wrapper.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>

using namespace sc_core;
using namespace tlm;


//======================================================
// Constructor
//======================================================

RiscvCore::RiscvCore(sc_module_name name)
    : sc_module(name),
      csr_socket("csr_socket"),
      memory_socket("memory_socket"),
      all_passed_(true)
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

    test_exp_vec();

    wait(50, SC_NS);

    test_softmax();

    wait(50, SC_NS);

    std::cout << "\nRISC-V program finished. Veredicto agregado: "
              << (all_passed_ ? "TODOS LOS TESTS PASARON" : "HAY TESTS FALLIDOS") << "\n";
}


//======================================================
// Registro centralizado del resultado de cada test
//======================================================
void RiscvCore::report_test(const char* name, bool pass)
{
    std::cout << name << (pass ? " TEST PASSED.\n" : " TEST FAILED.\n");
    if (!pass) all_passed_ = false;
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

    const uint32_t output_bytes = cgra_kernel_uses_fixed_vector_buffers(cgra_config)
                                      ? CGRA_VECTOR_OUTPUT_BYTES : data_size;

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

    if (cgra_kernel_uses_fixed_vector_buffers(cgra_config))
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

    report_test("VECTOR ADD", pass);
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

    report_test("FULL PIPELINE", pass);
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

    report_test("MATMUL 2x2", pass);
}

// e^u sobre los 4 lanes en punto fijo Q4.12, por el flujo completo
// RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper: el RISC-V provee los 4 valores
// de u, indica el kernel EXP_VEC, arranca la CGRA, espera DONE, lee el resultado
// desde Main Memory y lo verifica contra expf() en software. Toda la aritmetica
// (saturacion, range reduction base-2 y el polinomio grado 3) la hace la celda
// Vectorial; el RISC-V solo orquesta y verifica. Ver mesh_wrapper.cpp,
// run_exp_vec_dataflow.
//
// A diferencia del resto de la suite, el golden NO es una expresion entera exacta
// sino expf(), asi que la comparacion es por TOLERANCIA en LSB de Q4.12 y no por
// igualdad: el ISA no tiene exp, el kernel la construye, y lo que hay que demostrar
// es que el error de esa construccion se mantiene acotado. La tolerancia esta puesta
// apenas por encima del peor caso medido (1.25 LSB, barriendo exhaustivamente
// u en [-8,0]) a proposito -- si un cambio futuro degrada la precision, esto tiene
// que fallar, no absorberlo en un margen holgado.
void RiscvCore::test_exp_vec()
{
    std::cout << "\n=========================================\n";
    std::cout << "       Running EXP VEC TEST\n";
    std::cout << "  (e^u en Q4.12, computo integro en la celda Vectorial)\n";
    std::cout << "=========================================\n";

    cgra_config = EXP_VEC;

    input_addr  = 0x1000;
    output_addr = 0x2000;

    const double TOL_LSB = 2.0;

    // Corre un caso por el flujo completo y lo verifica lane por lane. u llega en
    // unidades reales; la conversion a Q4.12 y la saturacion esperada se calculan
    // aca para poder comparar contra el u que la CGRA realmente vio.
    auto run_case = [this, TOL_LSB](const char* title, const double u[4]) -> bool {
        std::cout << "\n-- " << title << " --\n";

        int32_t words[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            words[i] = static_cast<int32_t>(std::lround(u[i] * EXP_Q_ONE));
        }

        input_data.resize(CGRA_VECTOR_INPUT_BYTES);
        std::memcpy(input_data.data(), words, sizeof(words));
        data_size = input_data.size();

        // Golden: expf() sobre el u SATURADO. La saturacion a [EXP_Q_U_MIN, 0] es
        // comportamiento especificado del kernel (ver EXP_Q_U_MIN en
        // mesh_wrapper.h), no un efecto colateral, asi que los casos que la
        // ejercitan tienen que verificarla y no simplemente tolerarla.
        int32_t expected_q[4];
        double  u_sat[4];
        for (int i = 0; i < 4; ++i) {
            int32_t q = words[i];
            if (q < EXP_Q_U_MIN) q = EXP_Q_U_MIN;
            if (q > 0)           q = 0;
            u_sat[i] = static_cast<double>(q) / EXP_Q_ONE;
            expected_q[i] = static_cast<int32_t>(std::lround(std::exp(u_sat[i]) * EXP_Q_ONE));
        }

        golden_reference.resize(sizeof(expected_q));
        std::memcpy(golden_reference.data(), expected_q, sizeof(expected_q));

        //----------------------------------------------
        // Flujo CGRA completo.
        //----------------------------------------------
        load_input_data();
        configure_cgra();
        start_cgra();
        wait_for_completion();
        read_results();

        const int32_t* got = reinterpret_cast<const int32_t*>(output_data.data());

        bool   pass  = true;
        double worst = 0.0;
        std::cout << std::fixed;
        for (int i = 0; i < 4; ++i) {
            const double ref     = std::exp(u_sat[i]);
            const double val     = static_cast<double>(got[i]) / EXP_Q_ONE;
            const double err_lsb = std::fabs(val - ref) * EXP_Q_ONE;

            if (err_lsb > worst)   worst = err_lsb;
            if (err_lsb > TOL_LSB) pass  = false;

            std::cout << "    lane" << i
                      << "  u=" << std::setw(9) << std::setprecision(4) << u[i]
                      << (words[i] != static_cast<int32_t>(std::lround(u_sat[i] * EXP_Q_ONE))
                              ? " (sat " : "       ")
                      << std::setw(8) << u_sat[i]
                      << (words[i] != static_cast<int32_t>(std::lround(u_sat[i] * EXP_Q_ONE))
                              ? ")" : " ")
                      << "  esperado=" << std::setw(9) << std::setprecision(6) << ref
                      << "  obtenido=" << std::setw(9) << val
                      << "  err=" << std::setw(5) << std::setprecision(2) << err_lsb << " LSB\n";
        }
        std::cout << "  peor error del caso: " << std::setprecision(2) << worst
                  << " LSB (tolerancia " << TOL_LSB << ")\n";
        std::cout << (pass ? "  -> case OK\n" : "  -> case MISMATCH\n");
        return pass;
    };

    const double u1[4] = { 0.0,   -0.5,  -1.0,  -2.0  };   // rango tipico
    const double u2[4] = {-0.25,  -1.5,  -3.0,  -4.0  };   // fracciones no exactas
    const double u3[4] = {-5.0,   -6.0,  -7.0,  -8.0  };   // cerca del piso de underflow
    // Los dos casos que la saturacion existe para atrapar: sin ella el shamt de
    // OP_SRA se enmascara con 31 y ambos devolverian un valor absurdo EN SILENCIO
    // (u>0 -> shift negativo; u<-10.8 -> shift>=16 dando la vuelta).
    const double u4[4] = { 2.0,  -20.0,  -8.5,   0.5  };
    const double u5[4] = {-0.125, -2.75, -4.5,  -6.25 };   // 4 exponentes distintos

    bool pass = true;
    pass &= run_case("Caso 1: rango tipico, u en [-2, 0]", u1);
    pass &= run_case("Caso 2: fracciones no representables exactas", u2);
    pass &= run_case("Caso 3: cerca del piso de underflow del formato", u3);
    pass &= run_case("Caso 4: saturacion (u>0 y u muy negativo)", u4);
    pass &= run_case("Caso 5: lanes independientes de verdad", u5);

    report_test("EXP VEC", pass);
}

// Softmax sobre 4 lanes en Q4.12, por el flujo completo
// RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper. A diferencia del resto de la
// suite, este kernel reparte el computo entre DOS celdas de la malla: Vectorial hace
// las reducciones por butterfly, los 4 exponenciales y la normalizacion, y Escalar el
// reciproco 1/S por Newton-Raphson (ver mesh_wrapper.cpp, run_softmax_dataflow). El
// RISC-V solo orquesta y verifica.
//
// Verificacion por tolerancia en LSB contra el softmax en doble precision, mas dos
// invariantes que valen la pena chequear aparte porque fallan de formas distintas a un
// error de precision:
//   - sum(y) == 1: si una reduccion butterfly permuta mal los lanes, los y_i pueden
//     quedar individualmente plausibles pero no sumar 1.
//   - orden preservado: x_i > x_j  =>  y_i >= y_j. Detecta un cruce de lanes que
//     "casualmente" de valores cercanos a los correctos.
void RiscvCore::test_softmax()
{
    std::cout << "\n=========================================\n";
    std::cout << "       Running SOFTMAX TEST\n";
    std::cout << "  (Vectorial: reducciones + exp + normalizacion)\n";
    std::cout << "  (Escalar:   reciproco 1/S por Newton-Raphson)\n";
    std::cout << "=========================================\n";

    cgra_config = SOFTMAX;

    input_addr  = 0x1000;
    output_addr = 0x2000;

    const double TOL_LSB     = 6.0;   // peor caso modelado: 2.97 LSB por lane
    const double TOL_SUM_LSB = 8.0;   // peor caso modelado: 5.00 LSB en sum(y)

    auto run_case = [this, TOL_LSB, TOL_SUM_LSB](const char* title, const double x[4]) -> bool {
        std::cout << "\n-- " << title << " --\n";

        int32_t words[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            words[i] = static_cast<int32_t>(std::lround(x[i] * EXP_Q_ONE));
        }

        input_data.resize(CGRA_VECTOR_INPUT_BYTES);
        std::memcpy(input_data.data(), words, sizeof(words));
        data_size = input_data.size();

        // Golden en doble precision, calculado sobre los x YA cuantizados a Q4.12
        // (que es lo que la CGRA realmente recibe) para no cargarle al kernel el
        // error de redondeo de la entrada, que no es suyo.
        double ref[4];
        double xq[4];
        double mx = -1e300;
        for (int i = 0; i < 4; ++i) {
            xq[i] = static_cast<double>(words[i]) / EXP_Q_ONE;
            if (xq[i] > mx) mx = xq[i];
        }
        double acc = 0.0;
        for (int i = 0; i < 4; ++i) { ref[i] = std::exp(xq[i] - mx); acc += ref[i]; }
        for (int i = 0; i < 4; ++i) { ref[i] /= acc; }

        int32_t expected_q[4];
        for (int i = 0; i < 4; ++i) {
            expected_q[i] = static_cast<int32_t>(std::lround(ref[i] * EXP_Q_ONE));
        }
        golden_reference.resize(sizeof(expected_q));
        std::memcpy(golden_reference.data(), expected_q, sizeof(expected_q));

        //----------------------------------------------
        // Flujo CGRA completo.
        //----------------------------------------------
        load_input_data();
        configure_cgra();
        start_cgra();
        wait_for_completion();
        read_results();

        const int32_t* got = reinterpret_cast<const int32_t*>(output_data.data());

        bool   pass  = true;
        double worst = 0.0;
        int32_t sum_q = 0;

        std::cout << std::fixed;
        for (int i = 0; i < 4; ++i) {
            const double val     = static_cast<double>(got[i]) / EXP_Q_ONE;
            const double err_lsb = std::fabs(val - ref[i]) * EXP_Q_ONE;
            sum_q += got[i];

            if (err_lsb > worst)   worst = err_lsb;
            if (err_lsb > TOL_LSB) pass  = false;

            std::cout << "    lane" << i
                      << "  x=" << std::setw(8) << std::setprecision(4) << x[i]
                      << "  esperado=" << std::setw(9) << std::setprecision(6) << ref[i]
                      << "  obtenido=" << std::setw(9) << val
                      << "  err=" << std::setw(5) << std::setprecision(2) << err_lsb << " LSB\n";
        }

        // Invariante 1: sum(y) == 1.
        const double sum_err_lsb = std::fabs(static_cast<double>(sum_q) - EXP_Q_ONE);
        if (sum_err_lsb > TOL_SUM_LSB) pass = false;
        std::cout << "  sum(y) = " << std::setprecision(6)
                  << static_cast<double>(sum_q) / EXP_Q_ONE
                  << "  (desvio " << std::setprecision(2) << sum_err_lsb
                  << " LSB, tolerancia " << TOL_SUM_LSB << ")\n";

        // Invariante 2: el softmax es monotono, x_i > x_j => y_i >= y_j.
        bool order_ok = true;
        for (int i = 0; i < 4 && order_ok; ++i) {
            for (int j = 0; j < 4; ++j) {
                if (xq[i] > xq[j] && got[i] < got[j]) { order_ok = false; break; }
            }
        }
        if (!order_ok) pass = false;
        std::cout << "  orden preservado (x_i > x_j => y_i >= y_j): "
                  << (order_ok ? "si" : "NO") << "\n";

        std::cout << "  peor error del caso: " << std::setprecision(2) << worst
                  << " LSB (tolerancia " << TOL_LSB << ")\n";
        std::cout << (pass ? "  -> case OK\n" : "  -> case MISMATCH\n");
        return pass;
    };

    // Todos los lanes iguales: el caso degenerado. y = {0.25, 0.25, 0.25, 0.25} y S = 4
    // exactamente, el extremo superior del rango que asume la semilla del NR.
    const double x1[4] = { 1.0,  1.0,  1.0,  1.0 };
    // Rango tipico, maximo en el lane 0 y en el lane 3 (para que un butterfly que
    // asuma la posicion del maximo no pase inadvertido).
    const double x2[4] = { 2.0,  1.0,  0.0, -1.0 };
    const double x3[4] = {-1.0,  0.0,  1.0,  2.0 };
    // Muy concentrado: un lane domina, los otros caen al piso de underflow. S ~ 1, el
    // extremo INFERIOR del rango del NR.
    const double x4[4] = { 3.0, -3.0, -3.5, -4.0 };
    // Negativos y separaciones fraccionarias.
    const double x5[4] = {-2.25, -0.5, -3.75, -1.125 };
    // Spread mayor que el rango util de exp: fuerza la saturacion de EXP_Q_U_MIN.
    const double x6[4] = { 10.0, -8.0,  0.0, -20.0 };

    bool pass = true;
    pass &= run_case("Caso 1: todos los lanes iguales (y = 1/4 cada uno, S = 4)", x1);
    pass &= run_case("Caso 2: maximo en el lane 0", x2);
    pass &= run_case("Caso 3: maximo en el lane 3", x3);
    pass &= run_case("Caso 4: un lane domina (S ~ 1)", x4);
    pass &= run_case("Caso 5: todos negativos, separaciones fraccionarias", x5);
    pass &= run_case("Caso 6: spread > rango util de exp (satura)", x6);

    report_test("SOFTMAX", pass);
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