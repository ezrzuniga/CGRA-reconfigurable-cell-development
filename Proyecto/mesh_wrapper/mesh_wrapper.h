// mesh_wrapper.h
// Puente TLM-2.0 <-> senales planas sobre una CGRA_Mesh_Heterogeneous<2,2> real,
// layout {Enrutamiento, Memoria, Escalar, Vectorial} -- el mismo arreglo del
// diagrama de nivel 2 (ver Entrega_Avance_2/images/lvl2_diagram.png y
// mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp, cuya mecanica de programacion
// esta clase reusa 1:1 pero orquestada desde el protocolo CSR en vez de un
// testbench). Expone un unico tlm_utils::simple_target_socket con el mismo mapa
// de registros que riscv_dma_main_mem_components/csr_dma.cpp ya asume del lado
// "cgra_socket" (ver README.md, seccion "Mapa de registros").
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
#include "../pe/routing/PE_Routing_Cell.h"
#include "../memory/PE_Memory_Mesh_Cell.h"

// Catalogo de programas cargables via el registro CONFIG (0x00). El valor
// numerico de cada entrada coincide a proposito con el CGRA_KERNEL homonimo de
// riscv_dma_main_mem_components/cgra_kernel.h -- CONFIG no es un bitstream
// arbitrario, CSR_DMA reenvia cgra_config tal cual (ver csr_dma.cpp).
enum MeshProgram {
    // c = a + b, vía Celda Escalar + Celda Vectorial (Enrutamiento/Memoria
    // inactivas). Mismo resultado que la version <1,1> original -- preservado
    // para no romper el golden reference de RiscvCore::test_vector_add().
    PROGRAM_VECTOR_ADD = 0,

    // e = (a + b) * 2, recorriendo las 4 celdas del diagrama de nivel 2: b entra
    // por el borde norte real de Enrutamiento, se relevа hacia Memoria (NoC,
    // dir=NoC->SRAM), Memoria lo reenvia (dir=SRAM->NoC), Enrutamiento lo relay-ea
    // de vuelta hacia el sur (hacia Escalar); a entra directo por el borde oeste
    // real de Escalar. Escalar suma, Vectorial multiplica por 2 y expone el
    // resultado en su borde este real. Ver MeshWrapper::handle_start_write.
    PROGRAM_FULL_PIPELINE = 3,

    // Reduccion por suma: total = seed + sum(v[0..6]), tambien recorriendo las 4
    // celdas. INPUT_DATA_BUFFER (32 bytes) se reinterpreta como 8 enteros de 32
    // bits: word[0]=seed (viaja Enrutamiento->Memoria->Enrutamiento->Escalar,
    // igual que "b" en PROGRAM_FULL_PIPELINE) y word[1..7]=los 7 elementos del
    // vector a sumar (streameados uno por ciclo por el borde oeste real de
    // Escalar, acumulados con reg0 += west -- mismo patron que
    // pe/mac/PE_MAC_SumReduction__TB.cpp pero con el banco de registros de
    // Escalar en vez del acumulador dedicado de PE_MAC, que no existe en este
    // layout). Escalar reenvia el total a Vectorial, que lo expone en su borde
    // este real. Ver MeshWrapper::run_sum_reduction_dataflow.
    PROGRAM_SUM_REDUCTION = 4
};

// Cantidad de elementos del vector a reducir en PROGRAM_SUM_REDUCTION (7, no 8:
// el primer word de INPUT_DATA_BUFFER es el seed, no un elemento del vector).
static const int SUM_REDUCTION_VECTOR_LEN = 7;

SC_MODULE(MeshWrapper) {
public:
    tlm_utils::simple_target_socket<MeshWrapper> target_socket;

    SC_CTOR(MeshWrapper);

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

    // Forwarding al trace() del mesh interno + las senales de borde, mismo estilo
    // que CGRA_Mesh_Heterogeneous::trace.
    void trace(sc_core::sc_trace_file* tf) const;

private:
    // INSTR_MEM_SIZE=1: el PC de cada PE es un contador libre que siempre
    // incrementa (no hay saltos/branch en el ISA), asi que "pc % INSTR_MEM_SIZE"
    // solo vale 0 todo el tiempo si INSTR_MEM_SIZE=1 -- mismo motivo documentado en
    // mesh/README.md, seccion "Programar las PEs".
    typedef CGRA_Mesh_Heterogeneous<2, 2, 32, 4, 8, 1> Mesh;
    typedef Mesh::Link  Link;
    typedef Mesh::Instr Instr;

    static const int ROWS = 2;
    static const int COLS = 2;

    // Layout row-major (index = row*COLS+col), identico al del diagrama de nivel 2:
    //   (0,0) Enrutamiento   (0,1) Memoria
    //   (1,0) Escalar        (1,1) Vectorial
    // Bordes reales de esta malla 2x2 (ver mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp
    // para el detalle de la adyacencia):
    //   Enrutamiento (0,0): N, W       Memoria (0,1): N, E
    //   Escalar      (1,0): S, W      Vectorial (1,1): S, E

    // ---- Reloj/control propios, no expuestos como puertos ------------------
    sc_core::sc_clock       clk_;
    sc_core::sc_signal<bool> rst_;
    sc_core::sc_signal<bool> enable_;
    sc_core::sc_event        reset_done_event_;
    bool                     reset_done_;

    // ---- Malla y sus bordes --------------------------------------------------
    Mesh mesh_;
    sc_core::sc_signal<Link> in_N_[COLS], out_N_[COLS];
    sc_core::sc_signal<Link> in_S_[COLS], out_S_[COLS];
    sc_core::sc_signal<Link> in_W_[ROWS], out_W_[ROWS];
    sc_core::sc_signal<Link> in_E_[ROWS], out_E_[ROWS];

    // ---- Registros CSR expuestos por target_socket --------------------------
    uint32_t config_;
    uint32_t start_;
    uint32_t status_;
    uint32_t done_;
    bool     programmed_;
    Link     result_;

    // Elementos crudos de PROGRAM_SUM_REDUCTION, guardados en handle_input_write
    // (llega como un unico bloque TLM de 32 bytes) para reproducirlos uno por
    // ciclo despues, en run_sum_reduction_dataflow (disparado recien por START).
    int32_t sum_reduction_seed_;
    int32_t sum_reduction_vec_[SUM_REDUCTION_VECTOR_LEN];

    void reset_thread();

    void handle_config_write(uint32_t value);
    void handle_start_write(uint32_t value);
    void handle_input_write(const unsigned char* data, unsigned int len);
    void handle_output_read(unsigned char* data, unsigned int len);

    // Fases de PROGRAM_FULL_PIPELINE (ver .cpp): b viaja Enrutamiento->Memoria->
    // Enrutamiento->Escalar antes de que Escalar/Vectorial calculen el resultado.
    void run_full_pipeline_dataflow();

    // Fases de PROGRAM_SUM_REDUCTION (ver .cpp): el seed viaja Enrutamiento->
    // Memoria->Enrutamiento->Escalar (reg0=seed), despues Escalar acumula los 7
    // elementos de sum_reduction_vec_ uno por ciclo (reg0+=west) y reenvia el
    // total a Vectorial.
    void run_sum_reduction_dataflow();

    PE_Memory_Mesh_Cell<32, 4>& memory_cell();
};

#endif // MESH_WRAPPER_H
