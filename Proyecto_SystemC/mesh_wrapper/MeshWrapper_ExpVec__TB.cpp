// MeshWrapper_ExpVec__TB.cpp
// e^u sobre los 4 lanes en punto fijo Q4.12 (ver mesh_wrapper.h, PROGRAM_EXP_VEC)
// ejercitado por el FLUJO COMPLETO real: RiscvCore -> CSR_DMA -> MainMemory ->
// MeshWrapper. Mismo wiring que MeshWrapper_MatMul__TB.cpp y que
// riscv_dma_main_mem_components/RiscvDmaSystem__TB.cpp (incluido el MemoryRouter
// fan-in, porque MainMemory::socket es punto a punto y tanto RiscvCore como CSR_DMA
// necesitan acceso directo).
//
// El RISC-V provee los 4 valores de u, indica el kernel EXP_VEC, arranca la CGRA,
// espera DONE, lee el resultado desde Main Memory y lo verifica contra expf() en
// software -- por TOLERANCIA en LSB de Q4.12, no por igualdad, porque el ISA no tiene
// exp y lo que hay que demostrar es que el error de construirla con range reduction
// mas un polinomio grado 3 se mantiene acotado. Ver RiscvCore::test_exp_vec.
//
// RiscvCore::run() corre la suite completa (vector-add, full-pipeline, matmul y
// exp-vec); el veredicto agregado sale por RiscvCore::all_passed() y determina el
// exit code de este programa.

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <iostream>

#include "../riscv_dma_main_mem_components/riscv_core.h"
#include "../riscv_dma_main_mem_components/csr_dma.h"
#include "../riscv_dma_main_mem_components/main_memory.h"
#include "mesh_wrapper.h"

using namespace sc_core;
using namespace tlm;
using namespace std;

//----------------------------------------------------------------------
// MemoryRouter: adaptador fan-in solo para testbench. MainMemory::socket es un
// simple_target_socket (punto a punto, un unico bind), pero tanto RiscvCore como
// CSR_DMA necesitan acceso directo -- este modulo reenvia ambos target sockets al
// unico initiator socket bindeado a MainMemory. Identico al de
// MeshWrapper_MatMul__TB.cpp.
//----------------------------------------------------------------------
SC_MODULE(MemoryRouter) {
public:
    tlm_utils::simple_target_socket<MemoryRouter> cpu_target_socket;
    tlm_utils::simple_target_socket<MemoryRouter> dma_target_socket;
    tlm_utils::simple_initiator_socket<MemoryRouter> mem_socket;

    SC_CTOR(MemoryRouter)
        : cpu_target_socket("cpu_target_socket"),
          dma_target_socket("dma_target_socket"),
          mem_socket("mem_socket") {
        cpu_target_socket.register_b_transport(this, &MemoryRouter::b_transport);
        dma_target_socket.register_b_transport(this, &MemoryRouter::b_transport);
    }

    void b_transport(tlm_generic_payload& trans, sc_time& delay) {
        mem_socket->b_transport(trans, delay);
    }
};

int sc_main(int argc, char* argv[]) {
    MainMemory mem("mem");
    MemoryRouter mem_router("mem_router");
    CSR_DMA dma("dma");
    RiscvCore cpu("cpu");
    MeshWrapper cgra("cgra");

    cpu.csr_socket.bind(dma.target_socket);
    cpu.memory_socket.bind(mem_router.cpu_target_socket);
    dma.memory_socket.bind(mem_router.dma_target_socket);
    mem_router.mem_socket.bind(mem.socket);
    dma.cgra_socket.bind(cgra.target_socket);

    // Traza VCD del mesh interno: cgra.trace(tf) reenvia a mesh_.trace(tf) mas las
    // senales de control/borde. Util aca para ver, lane por lane, el shift variable
    // de la fase 7 (reg1 distinto en cada lane) que es la pieza central del kernel.
    sc_trace_file* tf = sc_create_vcd_trace_file("mesh_wrapper_exp_vec_wave");
    cgra.trace(tf);

    // Los 5 casos de exp-vec son 23 instrucciones x SETTLE(3) ciclos x 10 ns = 690 ns
    // cada uno, encima de la suite previa (vector-add, full-pipeline, matmul).
    sc_start(20, SC_US);

    sc_close_vcd_trace_file(tf);

    const bool pass = cpu.all_passed();
    cout << "\n" << (pass
        ? "PASS: suite completa (incluido EXP VEC) end-to-end via RiscvCore -> CSR_DMA "
          "-> MainMemory -> MeshWrapper.\n"
        : "FAIL: al menos un test de la suite no paso (ver el detalle arriba).\n");
    return pass ? 0 : 1;
}
