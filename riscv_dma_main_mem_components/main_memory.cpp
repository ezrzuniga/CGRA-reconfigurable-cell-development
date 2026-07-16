#include "main_memory.h"

#include <cstring>
#include <iostream>

using namespace std;

MainMemory::MainMemory(sc_module_name name): sc_module(name), socket("socket") {
    // Allocate memory space
    memory = new uint8_t[MEMORY_SIZE];

    // Initialize memory to zero
    memset(memory, 0, MEMORY_SIZE);

    // Register the transport method with the socket
    socket.register_b_transport(this, &MainMemory::b_transport);
}

void MainMemory::b_transport(tlm_generic_payload& trans, sc_time& delay) {
    // Get the command, address, data pointer, and data length from the transaction
    tlm_command cmd = trans.get_command();

    uint64_t addr = trans.get_address();

    unsigned char* data_ptr = trans.get_data_ptr();

    unsigned int data_length = trans.get_data_length();

    // Check for address out of bounds
    if (addr + data_length > MEMORY_SIZE) {
        // Address out of bounds
        trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
        return;
    };
    // check command type and perform the corresponding operation
    if (cmd == TLM_READ_COMMAND) {
        // Perform read operation
        memcpy(data_ptr, &memory[addr], data_length);
    } else if (cmd == TLM_WRITE_COMMAND) {
        // Perform write operation
        memcpy(&memory[addr], data_ptr, data_length);
    } else {
        // Invalid command
        trans.set_response_status(TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    delay += sc_time(10, SC_NS); // Simulate a delay of 10 ns for the operation

    // Set the response status to indicate successful completion
    trans.set_response_status(TLM_OK_RESPONSE);
}