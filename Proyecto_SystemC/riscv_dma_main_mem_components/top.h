#ifndef TOP_H
#define TOP_H

#include <systemc>

// Design modules
#include "main_memory.h"
#include "csr_dma.h"
#include "riscv_core.h"

// Verification module
#include "mesh_wrapper.h"

SC_MODULE(Top)
{
public:

    //--------------------------------------------------
    // System modules
    //--------------------------------------------------

    MainMemory memory;
    CSR_DMA dma;
    RiscvCore cpu;
    MeshWrapper cgra;


    //--------------------------------------------------
    // Constructor
    //--------------------------------------------------

    SC_CTOR(Top);

};

#endif