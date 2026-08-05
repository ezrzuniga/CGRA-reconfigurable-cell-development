// MeshWrapper_Softmax__TB.cpp
// Softmax sobre los 4 lanes en punto fijo Q4.12 (ver mesh_wrapper.h, PROGRAM_SOFTMAX)
// ejercitado por el FLUJO COMPLETO real: RiscvCore -> CSR_DMA -> MainMemory ->
// MeshWrapper. Mismo wiring que MeshWrapper_ExpVec__TB / MeshWrapper_MatMul__TB
// (incluido el MemoryRouter fan-in, porque MainMemory::socket es punto a punto y tanto
// RiscvCore como CSR_DMA necesitan acceso directo).
//
// Es el unico kernel del catalogo que reparte el computo entre DOS celdas de la malla
// por razones arquitectonicas, con ida y vuelta real sobre el enlace interno
// Escalar<->Vectorial:
//
//   Vectorial (1,1): reducciones por butterfly (max y suma), resta del maximo, los 4
//                    exponenciales en paralelo y la normalizacion final.
//   Escalar   (1,0): el reciproco 1/S por Newton-Raphson -- S es un solo numero, y la
//                    salida de Escalar difunde lane 0 a las 4 lanes, que es
//                    exactamente como Vectorial necesita r de vuelta.
//
// La verificacion vive en RiscvCore::test_softmax y combina tolerancia en LSB contra
// el softmax en doble precision con dos invariantes estructurales (sum(y)==1 y
// monotonia), que atrapan errores de permutacion de lanes que una comparacion por
// tolerancia sola dejaria pasar.
//
// RiscvCore::run() corre la suite completa; el veredicto agregado sale por
// RiscvCore::all_passed() y determina el exit code de este programa.

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
// unico initiator socket bindeado a MainMemory.
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

    // Traza VCD del mesh interno. Util aca sobre todo para ver el ping-pong
    // Escalar<->Vectorial de las fases E-G, que no aparece en ningun otro kernel.
    sc_trace_file* tf = sc_create_vcd_trace_file("mesh_wrapper_softmax_wave");
    cgra.trace(tf);

    // Softmax son ~57 instrucciones x SETTLE(3) ciclos x 10 ns = ~1.7 us por caso, y
    // son 6 casos, encima de la suite previa (vector-add, full-pipeline, matmul,
    // exp-vec). 60 us queda holgado.
    sc_start(60, SC_US);

    sc_close_vcd_trace_file(tf);

    const bool pass = cpu.all_passed();
    cout << "\n" << (pass
        ? "PASS: suite completa (incluido SOFTMAX) end-to-end via RiscvCore -> CSR_DMA "
          "-> MainMemory -> MeshWrapper.\n"
        : "FAIL: al menos un test de la suite no paso (ver el detalle arriba).\n");
    return pass ? 0 : 1;
}
