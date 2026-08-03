#ifndef MAIN_MEMORY_H
#define MAIN_MEMORY_H

#include <cstdint>
#include <systemc>
#include "tlm"
#include "tlm_utils/simple_target_socket.h"

using namespace sc_core;
using namespace tlm;

SC_MODULE(MainMemory) {
public:
    // TLM socket for communication with other modules
    tlm_utils::simple_target_socket<MainMemory> socket;

    // Memory size in bytes(64MB)
    static const std::size_t MEMORY_SIZE = 64 * 1024 * 1024;

    // Constructor
    SC_CTOR(MainMemory);

    // TLM transport function
    void b_transport(tlm_generic_payload& trans, sc_time& delay);

private:
    // pointer to the memory array
    uint8_t* memory;
};
#endif // MAIN_MEMORY_H