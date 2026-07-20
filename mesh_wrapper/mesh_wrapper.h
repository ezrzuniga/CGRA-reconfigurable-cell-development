// mesh_wrapper.h
// Puente TLM-2.0 <-> senales planas sobre una CGRA_Mesh_Heterogeneous<1,1> de una
// sola PE_vector. Expone un unico tlm_utils::simple_target_socket con el mismo mapa
// de registros que riscv_dma_main_mem_components/csr_dma.cpp ya asume del lado
// "cgra_socket" (ver README.md, seccion "Mapa de registros"), y traduce cada
// transaccion en mesh.load_instr(...)/pokes sobre los puertos de borde del mesh real.
//
// clk/rst/enable del mesh son enteramente internos: MeshWrapper genera su propio
// sc_clock y se resetea una unica vez al arrancar la simulacion. Nadie fuera de este
// modulo necesita tocarlos -- toda la programacion/control pasa por target_socket.

#ifndef MESH_WRAPPER_H
#define MESH_WRAPPER_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>

#include "../mesh/CGRA_Mesh_Heterogeneous.h"

// Catalogo de programas cargables via el registro CONFIG (0x00). Hoy solo hay una
// entrada; agregar una nueva implica: (1) un valor de enum aqui, (2) una rama nueva
// en MeshWrapper::build_instruction, (3) actualizar la tabla del README.
enum MeshProgram {
    PROGRAM_VECTOR_ADD = 0   // opcode=OP_ADD, src_a=SRC_WEST, src_b=SRC_NORTH, dst=DST_EAST
};

SC_MODULE(MeshWrapper) {
public:
    tlm_utils::simple_target_socket<MeshWrapper> target_socket;

    SC_CTOR(MeshWrapper);

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

    // Forwarding al trace() del mesh interno + las senales de borde, mismo estilo
    // que CGRA_Mesh_Heterogeneous::trace.
    void trace(sc_core::sc_trace_file* tf) const;

private:
    // INSTR_MEM_SIZE=1 (no 16): el PC de la PE es un contador libre que siempre
    // incrementa (no hay saltos/branch en el ISA), asi que "pc % INSTR_MEM_SIZE" solo
    // vale 0 todo el tiempo si INSTR_MEM_SIZE=1 -- mismo motivo por el que
    // mesh/CGRA_Mesh_SmokeTest__TB.cpp/ComplexTest__TB.cpp usan siempre addr=0 con
    // INSTR_MEM_SIZE=1 (ver mesh/README.md, seccion "Programar las PEs").
    typedef CGRA_Mesh_Heterogeneous<1, 1, 32, 4, 8, 1> Mesh;
    typedef Mesh::Link  Link;
    typedef Mesh::Instr Instr;

    // ---- Reloj/control propios, no expuestos como puertos ------------------
    sc_core::sc_clock       clk_;
    sc_core::sc_signal<bool> rst_;
    sc_core::sc_signal<bool> enable_;
    sc_core::sc_event        reset_done_event_;
    bool                     reset_done_;

    // ---- Malla y sus bordes (ROWS=COLS=1 -> los 4 bordes son reales) --------
    Mesh mesh_;
    sc_core::sc_signal<Link> in_N_, in_S_, in_W_, in_E_;
    sc_core::sc_signal<Link> out_N_, out_S_, out_W_, out_E_;

    // ---- Registros CSR expuestos por target_socket --------------------------
    uint32_t config_;
    uint32_t start_;
    uint32_t status_;
    uint32_t done_;
    bool     programmed_;
    Link     result_;

    void reset_thread();

    void handle_config_write(uint32_t value);
    void handle_start_write(uint32_t value);
    void handle_input_write(const unsigned char* data, unsigned int len);
    void handle_output_read(unsigned char* data, unsigned int len);

    Instr build_instruction(uint32_t prog_value, bool& ok);
};

#endif // MESH_WRAPPER_H
