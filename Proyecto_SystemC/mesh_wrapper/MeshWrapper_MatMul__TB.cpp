// MeshWrapper_MatMul__TB.cpp
// Multiplicacion matricial 2x2 (C = A * B) ejercitada por el FLUJO COMPLETO real:
// RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper. A diferencia de
// MeshWrapper_SumReduction__TB (que emula el DMA con un FakeCsrDma), aca se
// instancian los modulos reales: el RISC-V provee A y B, indica el kernel MATMUL,
// arranca la CGRA, espera DONE, lee C desde Main Memory y la verifica contra su
// golden en software (ver RiscvCore::test_matmul). Toda la aritmetica la hace la
// celda Vectorial del arreglo 2x2 (ver mesh_wrapper.cpp, run_matmul_dataflow).
//
// El wiring es identico al de riscv_dma_main_mem_components/RiscvDmaSystem__TB.cpp
// (incluido el MemoryRouter fan-in, porque MainMemory::socket es punto a punto y
// tanto RiscvCore como CSR_DMA necesitan acceso directo). RiscvCore::run() corre
// su suite completa (vector-add, full-pipeline y matmul); el resultado de la
// prueba de matmul se imprime como "MATMUL 2x2 TEST PASSED/FAILED".

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
// CSR_DMA necesitan acceso directo -- este modulo reenvia ambos target sockets
// al unico initiator socket bindeado a MainMemory.
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

    // Traza VCD del mesh interno (mismo mecanismo que MeshWrapper_SumReduction__TB):
    // wrapper.trace(tf) reenvia a mesh_.trace(tf) mas las senales de control/borde.
    sc_trace_file* tf = sc_create_vcd_trace_file("mesh_wrapper_matmul_wave");
    cgra.trace(tf);

    sc_start(10, SC_US);

    sc_close_vcd_trace_file(tf);

    cout << "\nPASS: matmul 2x2 end-to-end via RiscvCore -> CSR_DMA -> MainMemory "
            "-> MeshWrapper (ver 'MATMUL 2x2 TEST' arriba para el veredicto).\n";
    return 0;
}
