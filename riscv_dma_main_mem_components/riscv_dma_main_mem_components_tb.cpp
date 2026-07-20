//==============================================================
// File: riscv_dma_main_mem_components_tb.cpp
//
// Description:
// Entry point of the SystemC simulation.
// It instantiates the complete CGRA platform and starts the
// SystemC kernel.
//==============================================================

#include <systemc>
#include <iostream>

#include "top.h"

int sc_main(int argc, char* argv[])
{
    std::cout << "=========================================\n";
    std::cout << "   CGRA Platform Simulation Started\n";
    std::cout << "=========================================\n\n";

    // Instantiate the complete platform.
    Top top("TOP");

    // Start the SystemC simulation.
    sc_start();

    std::cout << "\n=========================================\n";
    std::cout << "   CGRA Platform Simulation Finished\n";
    std::cout << "=========================================\n";

    return 0;
}