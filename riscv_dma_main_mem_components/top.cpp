#include "top.h"

//======================================================
// Constructor
//======================================================

Top::Top(sc_module_name name)
    : sc_module(name),
      memory("memory"),
      dma("dma"),
      cpu("cpu"),
      cgra("cgra")
{

    //--------------------------------------------------
    // RISC-V Core connections
    //--------------------------------------------------

    // CPU <----> Main Memory
    cpu.memory_socket.bind(memory.socket);

    // CPU <----> CSR/DMA bridge
    cpu.csr_socket.bind(dma.target_socket);


    //--------------------------------------------------
    // CSR/DMA Bridge connections
    //--------------------------------------------------

    // DMA <----> Main Memory
    dma.memory_socket.bind(memory.socket);

    // DMA <----> Dummy CGRA
    dma.cgra_socket.bind(cgra.socket);

}