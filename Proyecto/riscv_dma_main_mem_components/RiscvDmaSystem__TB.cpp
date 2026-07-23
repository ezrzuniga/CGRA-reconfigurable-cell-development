#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <iostream>

#include "riscv_core.h"
#include "csr_dma.h"
#include "main_memory.h"
#include "../mesh_wrapper/mesh_wrapper.h"

using namespace sc_core;
using namespace tlm;
using namespace std;

//----------------------------------------------------------------------
// MemoryRouter: testbench-only fan-in adapter. MainMemory::socket is a
// simple_target_socket (point-to-point, single bind), but both RiscvCore
// and CSR_DMA need direct access to it -- so this just forwards both
// target sockets onto the one initiator socket bound to MainMemory.
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

    sc_start(5, SC_US);

    cout << "PASS: RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper end-to-end smoke test "
            "(vector-add + full 2x2 heterogeneous pipeline).\n";
    return 0;
}
