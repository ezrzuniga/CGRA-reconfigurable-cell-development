// MeshWrapper_CSRDMA_Sim__TB.cpp
// Imita a grandes rasgos como riscv_dma_main_mem_components/CSR_DMA programaria y
// controlaria un CGRA real: FakeCsrDma reproduce, transaccion por transaccion, la
// misma secuencia que CSR_DMA::dma_controller() hace contra su cgra_socket
// (ver csr_dma.cpp: send_configuration -> send_input_data_to_cgra -> execute_cgra
// (START + poll DONE) -> receive_output_data_from_cgra), pero contra MeshWrapper en
// vez de un CSR_DMA/RiscvCore/MainMemory reales -- el objetivo es documentar y
// probar el protocolo de programacion/control de mesh_wrapper de forma autocontenida.

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <array>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <sstream>

#include "mesh_wrapper.h"
#include "../pe/test_util.h"

using namespace sc_core;
using namespace tlm;

typedef std::array<int32_t, 4> Vec4;

static std::string describe(const Vec4& a, const Vec4& b) {
    std::ostringstream os;
    os << "A={" << a[0] << "," << a[1] << "," << a[2] << "," << a[3] << "} "
       << "B={" << b[0] << "," << b[1] << "," << b[2] << "," << b[3] << "}";
    return os.str();
}

SC_MODULE(FakeCsrDma) {
public:
    tlm_utils::simple_initiator_socket<FakeCsrDma> cgra_socket;

    SC_CTOR(FakeCsrDma) : cgra_socket("cgra_socket"), all_ok_(true) {
        SC_THREAD(run);
    }

    bool passed() const { return all_ok_; }

private:
    bool all_ok_;

    void run();
    void run_add_round(const std::string& label, const Vec4& a, const Vec4& b, const Vec4& expected);

    void write_word(uint64_t addr, uint32_t value);
    uint32_t read_word(uint64_t addr);
    void write_buffer(uint64_t addr, const unsigned char* data, unsigned int len);
    void read_buffer(uint64_t addr, unsigned char* data, unsigned int len);
};

void FakeCsrDma::write_word(uint64_t addr, uint32_t value) {
    sc_time delay = SC_ZERO_TIME;
    tlm_generic_payload trans;
    trans.set_command(TLM_WRITE_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
    trans.set_data_length(sizeof(uint32_t));
    trans.set_streaming_width(sizeof(uint32_t));
    cgra_socket->b_transport(trans, delay);
}

uint32_t FakeCsrDma::read_word(uint64_t addr) {
    sc_time delay = SC_ZERO_TIME;
    tlm_generic_payload trans;
    uint32_t value = 0;
    trans.set_command(TLM_READ_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
    trans.set_data_length(sizeof(uint32_t));
    trans.set_streaming_width(sizeof(uint32_t));
    cgra_socket->b_transport(trans, delay);
    return value;
}

void FakeCsrDma::write_buffer(uint64_t addr, const unsigned char* data, unsigned int len) {
    sc_time delay = SC_ZERO_TIME;
    tlm_generic_payload trans;
    trans.set_command(TLM_WRITE_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(const_cast<unsigned char*>(data));
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    cgra_socket->b_transport(trans, delay);
}

void FakeCsrDma::read_buffer(uint64_t addr, unsigned char* data, unsigned int len) {
    sc_time delay = SC_ZERO_TIME;
    tlm_generic_payload trans;
    trans.set_command(TLM_READ_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(data);
    trans.set_data_length(len);
    trans.set_streaming_width(len);
    cgra_socket->b_transport(trans, delay);
}

// Reproduce send_input_data_to_cgra() -> execute_cgra() (START + poll DONE) ->
// receive_output_data_from_cgra() de csr_dma.cpp, sin repetir CONFIG.
void FakeCsrDma::run_add_round(const std::string& label, const Vec4& a, const Vec4& b, const Vec4& expected) {
    unsigned char in_buf[32];
    std::memcpy(in_buf, a.data(), 16);
    std::memcpy(in_buf + 16, b.data(), 16);
    write_buffer(0x10, in_buf, 32);

    write_word(0x04, 1);

    uint32_t done = 0;
    do {
        done = read_word(0x0C);
        if (!done) wait(10, SC_NS);
    } while (!done);

    unsigned char out_buf[16];
    read_buffer(0x14, out_buf, 16);
    int32_t out[4];
    std::memcpy(out, out_buf, sizeof(out));

    PE_VectorData<32, 4> got, exp;
    for (int i = 0; i < 4; ++i) { got[i] = out[i]; exp[i] = expected[i]; }
    test_check(all_ok_, label, describe(a, b), exp, got);

    uint32_t status_after = read_word(0x08);
    test_check_bool(all_ok_, label + " (STATUS en reposo tras DONE)",
                     "status==0 esperado", status_after == 0);
}

void FakeCsrDma::run() {
    test_section("CONFIG: programar PROGRAM_VECTOR_ADD");
    write_word(0x00, PROGRAM_VECTOR_ADD);

    test_section("Ronda 1");
    run_add_round("Ronda 1: c = a + b", {1, 2, 3, 4}, {10, 20, 30, 40}, {11, 22, 33, 44});

    test_section("Ronda 2 (sin repetir CONFIG -- el programa sigue cargado)");
    run_add_round("Ronda 2: c = a + b", {-5, 0, 100, 7}, {5, 1, -100, 3}, {0, 1, 0, 10});

    test_section("CONFIG invalido (99): se espera SC_REPORT_ERROR en consola, sin abortar");
    write_word(0x00, 99);

    test_section("Recuperacion: reprogramar y repetir Ronda 1");
    write_word(0x00, PROGRAM_VECTOR_ADD);
    run_add_round("Ronda 1 (post-recuperacion): c = a + b",
                  {1, 2, 3, 4}, {10, 20, 30, 40}, {11, 22, 33, 44});

    std::cout << (all_ok_ ? "\nPASS: mesh_wrapper protocolo CONFIG/START/DONE.\n"
                           : "\nFAIL: mesh_wrapper protocolo CONFIG/START/DONE.\n");
}

int sc_main(int argc, char* argv[]) {
    // Por defecto, SC_REPORT_ERROR agrega SC_THROW a sus acciones (IEEE 1666-2023) --
    // lanzaria una excepcion no capturada y abortaria la simulacion completa apenas
    // MeshWrapper reporta un CONFIG invalido. Ese caso es exactamente lo que el test
    // "CONFIG invalido" quiere ejercitar sin abortar, asi que se lo deja en
    // DISPLAY+LOG (mismo nivel que un WARNING), preservando la severidad ERROR en el
    // mensaje pero sin la excepcion.
    sc_report_handler::set_actions(SC_ERROR, SC_DISPLAY | SC_LOG);

    MeshWrapper wrapper("wrapper");
    FakeCsrDma dma("fake_csr_dma");
    dma.cgra_socket.bind(wrapper.target_socket);

    sc_trace_file* tf = sc_create_vcd_trace_file("mesh_wrapper_csr_dma_sim_wave");
    wrapper.trace(tf);

    // Margen explicito: todo el trabajo ocurre dentro del SC_THREAD de FakeCsrDma
    // (termina solo al completar run()); 500 ns es holgado frente a las ~12
    // transacciones x ~20-30ns que hace el protocolo completo de este test.
    sc_start(500, SC_NS);

    sc_close_vcd_trace_file(tf);

    bool pass = dma.passed();
    std::cout << (pass ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return pass ? 0 : 1;
}
