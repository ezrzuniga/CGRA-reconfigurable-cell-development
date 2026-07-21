#ifndef TOP_H
#define TOP_H

#include <systemc>

// Design modules
#include "main_memory.h"
#include "csr_dma.h"
#include "riscv_core.h"

// Verification module
#include "dummy_cgra.h"

SC_MODULE(Top)
{
public:

    //--------------------------------------------------
    // System modules
    //--------------------------------------------------

    MainMemory memory;
    CSR_DMA dma;
    RiscvCore cpu;
    DummyCGRA cgra;


    //--------------------------------------------------
    // Constructor
    //--------------------------------------------------

    SC_CTOR(Top);

};

#endif