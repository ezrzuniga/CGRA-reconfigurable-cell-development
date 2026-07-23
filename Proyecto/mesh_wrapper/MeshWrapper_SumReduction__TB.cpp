// MeshWrapper_SumReduction__TB.cpp
// Reduccion por suma (sumar todos los elementos de un vector) ejercitando el
// pipeline completo del arreglo CGRA 2x2 (Enrutamiento, Memoria, Escalar,
// Vectorial -- ver mesh_wrapper.h, PROGRAM_SUM_REDUCTION) a traves del protocolo
// CSR real: FakeCsrDma reproduce, transaccion por transaccion, la misma secuencia
// que CSR_DMA::dma_controller() hace contra su cgra_socket (mismo patron que
// MeshWrapper_CSRDMA_Sim__TB.cpp), pero contra MeshWrapper en vez de un
// CSR_DMA/RiscvCore/MainMemory reales.
//
// A diferencia de PROGRAM_VECTOR_ADD/PROGRAM_FULL_PIPELINE (2 operandos de 4
// lanes cada uno), INPUT_DATA_BUFFER se reinterpreta aqui como 8 enteros de 32
// bits: word[0] = seed (recorre Enrutamiento->Memoria->Enrutamiento->Escalar,
// igual que el operando "b" de PROGRAM_FULL_PIPELINE) y word[1..7] = los 7
// elementos del vector a sumar, que Escalar acumula uno por ciclo en su banco de
// registros (reg0 += oeste) antes de reenviar el total a Vectorial. Ver el
// comentario de MeshWrapper::run_sum_reduction_dataflow (mesh_wrapper.cpp) para
// el detalle fase por fase.

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

typedef std::array<int32_t, SUM_REDUCTION_VECTOR_LEN> SumVec;

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
    void run_sum_reduction_round(const std::string& label, int32_t seed,
                                  const SumVec& v, int32_t expected_total);

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
// receive_output_data_from_cgra() de csr_dma.cpp, sin repetir CONFIG (ya escrito
// por run() antes de llamar aqui).
void FakeCsrDma::run_sum_reduction_round(const std::string& label, int32_t seed,
                                          const SumVec& v, int32_t expected_total) {
    unsigned char in_buf[32];
    int32_t words[8];
    words[0] = seed;
    for (int i = 0; i < SUM_REDUCTION_VECTOR_LEN; ++i) words[i + 1] = v[i];
    std::memcpy(in_buf, words, sizeof(words));
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

    std::ostringstream in;
    in << "seed=" << seed << " v={";
    for (int i = 0; i < SUM_REDUCTION_VECTOR_LEN; ++i) {
        in << v[i];
        if (i + 1 < SUM_REDUCTION_VECTOR_LEN) in << ",";
    }
    in << "}";

    PE_VectorData<32, 4> got, exp;
    for (int i = 0; i < 4; ++i) { got[i] = out[i]; exp[i] = expected_total; }
    test_check(all_ok_, label, in.str(), exp, got);

    uint32_t status_after = read_word(0x08);
    test_check_bool(all_ok_, label + " (STATUS en reposo tras DONE)",
                     "status==0 esperado", status_after == 0);
}

void FakeCsrDma::run() {
    test_section("CONFIG: programar PROGRAM_SUM_REDUCTION");
    write_word(0x00, PROGRAM_SUM_REDUCTION);

    test_section("Ronda 1: seed=100, v={6,-2,9,4,0,7,-5} (suma=19)");
    run_sum_reduction_round("Ronda 1: total = seed + sum(v)", 100,
                             SumVec{6, -2, 9, 4, 0, 7, -5}, 119);

    test_section("Ronda 2 (sin repetir CONFIG): seed=0, v={1,2,3,4,5,6,7} (suma=28)");
    run_sum_reduction_round("Ronda 2: total = seed + sum(v)", 0,
                             SumVec{1, 2, 3, 4, 5, 6, 7}, 28);

    test_section("Ronda 3: seed negativo, mezcla de signos");
    run_sum_reduction_round("Ronda 3: total = seed + sum(v)", -50,
                             SumVec{10, -20, 30, -40, 50, -60, 70}, -10);

    std::cout << (all_ok_ ? "\nPASS: reduccion por suma end-to-end via CSR "
                             "(Enrutamiento+Memoria+Escalar+Vectorial).\n"
                           : "\nFAIL: reduccion por suma end-to-end via CSR.\n");
}

int sc_main(int argc, char* argv[]) {
    MeshWrapper wrapper("wrapper");
    FakeCsrDma dma("fake_csr_dma");
    dma.cgra_socket.bind(wrapper.target_socket);

    sc_trace_file* tf = sc_create_vcd_trace_file("mesh_wrapper_sum_reduction_wave");
    wrapper.trace(tf);

    // Cada ronda hace ~20 transacciones de mesh_.load_instr (10ns cada una) mas
    // las esperas de polling del DMA local de Memoria y el streaming de 7
    // elementos; 3000 ns por ronda x 3 rondas queda holgado.
    sc_start(9000, SC_NS);

    sc_close_vcd_trace_file(tf);

    bool pass = dma.passed();
    std::cout << (pass ? "\n=== RESULTADO: PASS ===\n" : "\n=== RESULTADO: FAIL ===\n");
    return pass ? 0 : 1;
}
