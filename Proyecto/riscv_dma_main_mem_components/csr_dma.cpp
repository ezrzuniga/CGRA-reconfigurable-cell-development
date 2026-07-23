#include "csr_dma.h"
#include "dummy_cgra.h"

#include <cstring>
#include <iostream>

using namespace std;

CSR_DMA::CSR_DMA(sc_module_name name) : sc_module(name), target_socket("target_socket"),
                                         memory_socket("memory_socket"), cgra_socket("cgra_socket") {

    target_socket.register_b_transport(this, &CSR_DMA::b_transport);

    SC_THREAD(dma_controller);

    // Initialize registers
    cgra_config = 0;
    input_addr = 0;
    output_addr = 0;
    data_size = 0;
    start = 0;
    status = 0;
    done = 0;
    input_buffer.clear();
    output_buffer.clear();
}
// memory-mapped Control and Status Registers (CSRs)
// Address         Register
// ----------------------------------
// 0x00            CGRA_CONFIG
// 0x04            INPUT_ADDR
// 0x08            OUTPUT_ADDR
// 0x0C            DATA_SIZE
// 0x10            START
// 0x14            STATUS
// 0x18            DONE
// ----------------------------------

//------------------------------
// we suppose CGRA Register Map
//
// 0x00 -> CONFIG
//
// 0x04 -> START
//
// 0x08 -> STATUS
//
// 0x0C -> DONE
//
// 0x10 -> INPUT DATA BUFFER
//
// 0x14 -> OUTPUT DATA BUFFER
//-----------------------------

void CSR_DMA::b_transport(tlm_generic_payload& trans, sc_time& delay) {
    // Get the command, address, data pointer, and data length from the transaction
    uint64_t addr = trans.get_address();
    uint32_t* data_ptr = reinterpret_cast<uint32_t*>(trans.get_data_ptr());
    tlm_command cmd = trans.get_command();
    // check if the command is a read or write operation and perform the corresponding action
    if(cmd == TLM_WRITE_COMMAND) {
        // Write operation
        switch (addr) {
            case 0x00: 
                cgra_config = *data_ptr; 
                break;
            case 0x04: 
                input_addr = *data_ptr; 
                break;
            case 0x08: 
                output_addr = *data_ptr; 
                break;
            case 0x0C: 
                data_size = *data_ptr; 
                break;
            case 0x10:
                start = *data_ptr;
                if(start == 1) {
                    // done se limpia aqui, sincronicamente, no dentro de
                    // dma_controller(): start_dma_event.notify() solo programa a
                    // dma_controller para el proximo delta cycle, no lo ejecuta de
                    // inmediato. Si un segundo kernel arranca en la misma
                    // simulacion (ej. RiscvCore corriendo dos tests seguidos),
                    // RiscvCore::wait_for_completion() puede leer `done` ANTES de
                    // que dma_controller() alcance a poner done=0 el, viendo el
                    // done=1 que quedo del kernel anterior y devolviendo de
                    // inmediato con resultados viejos.
                    done = 0;
                    start_dma_event.notify();
                }
                break;
        };
    } else if(cmd == TLM_READ_COMMAND) {
        // Read operation
        switch (addr) {
            case 0x14: 
                *data_ptr = status; 
                break;
            case 0x18: 
                *data_ptr = done; 
                break;
        }
    }

    trans.set_response_status(TLM_OK_RESPONSE);
}

void CSR_DMA::dma_controller()
{
    while(true)
    {
        wait(start_dma_event);

        status = 1;
        done = 0;

        //----------------------------------
        // Fetch the input data.
        //----------------------------------
        read_from_memory();
        //----------------------------------
        // Configure the accelerator (CGRA)
        //----------------------------------
        send_configuration();
        //----------------------------------
        // Send the input data.
        //----------------------------------
        send_input_data_to_cgra();
        //----------------------------------
        // Execute the computation.
        //----------------------------------
        execute_cgra();
        //----------------------------------
        // Retrieve the results.
        //----------------------------------
        receive_output_data_from_cgra();
        //----------------------------------
        // Store them in Main Memory.
        //----------------------------------
        write_results();

        status = 0;
        done = 1;
    }
}

void CSR_DMA::read_from_memory() {
    sc_time delay = SC_ZERO_TIME;

    tlm_generic_payload trans;


    const uint32_t input_bytes = (cgra_config == VECTOR_ADD || cgra_config == FULL_PIPELINE) ? 32 : data_size;

    input_buffer.resize(input_bytes);

    trans.set_command(TLM_READ_COMMAND);

    trans.set_address(input_addr);

    trans.set_data_ptr(input_buffer.data());

    trans.set_data_length(input_bytes);

    trans.set_streaming_width(input_bytes);

    memory_socket->b_transport(trans, delay);

    if(trans.get_response_status()
        != TLM_OK_RESPONSE)
    {
        SC_REPORT_ERROR(
            "CSR_DMA",
            "Main Memory read failed.");
    }
}

void CSR_DMA::send_configuration() {
    sc_time delay = SC_ZERO_TIME;

    tlm_generic_payload trans;

    uint32_t config = cgra_config;


    trans.set_command(TLM_WRITE_COMMAND);

    // Configuration register inside CGRA
    trans.set_address(0x00);

    trans.set_data_ptr(
            reinterpret_cast<unsigned char*>(&config));

    trans.set_data_length(sizeof(uint32_t));

    trans.set_streaming_width(sizeof(uint32_t));


    cgra_socket->b_transport(trans, delay);


    if(trans.get_response_status() != TLM_OK_RESPONSE)
    {
        SC_REPORT_ERROR("DMA",
                        "CGRA configuration failed.");
    }
}

void CSR_DMA::send_input_data_to_cgra(){
    sc_time delay = SC_ZERO_TIME;

    tlm_generic_payload trans;


    const uint32_t input_bytes = (cgra_config == VECTOR_ADD || cgra_config == FULL_PIPELINE) ? 32 : data_size;

    trans.set_command(TLM_WRITE_COMMAND);

    trans.set_address(0x10);

    trans.set_data_ptr(input_buffer.data());

    trans.set_data_length(input_bytes);

    trans.set_streaming_width(input_bytes);

    cgra_socket->b_transport(trans, delay);

    if(trans.get_response_status()
        != TLM_OK_RESPONSE)
    {
        SC_REPORT_ERROR(
            "CSR_DMA",
            "Failed to send the input data to the CGRA.");
    }

}

void CSR_DMA::execute_cgra() {
    sc_time delay = SC_ZERO_TIME;

    tlm_generic_payload trans;

    uint32_t start_signal = 1;


    trans.set_command(TLM_WRITE_COMMAND);

    trans.set_address(0x04);

    trans.set_data_ptr(
        reinterpret_cast<unsigned char*>(&start_signal));

    trans.set_data_length(sizeof(uint32_t));

    trans.set_streaming_width(sizeof(uint32_t));


    cgra_socket->b_transport(trans, delay);


    //---------------------------------
    // Wait until CGRA finishes
    //---------------------------------

    uint32_t done = 0;

    while(!done)
    {
        tlm_generic_payload read_trans;

        read_trans.set_command(TLM_READ_COMMAND);

        read_trans.set_address(0x0C);

        read_trans.set_data_ptr(
            reinterpret_cast<unsigned char*>(&done));

        read_trans.set_data_length(sizeof(uint32_t));

        read_trans.set_streaming_width(sizeof(uint32_t));


        cgra_socket->b_transport(
                read_trans,
                delay);


        wait(10, SC_NS);
    }
}

void CSR_DMA::receive_output_data_from_cgra()
{
    sc_time delay = SC_ZERO_TIME;

    tlm_generic_payload trans;

    const uint32_t output_bytes = (cgra_config == VECTOR_ADD || cgra_config == FULL_PIPELINE) ? 16 : data_size;

    output_buffer.resize(output_bytes);

    trans.set_command(TLM_READ_COMMAND);

    trans.set_address(0x14);

    trans.set_data_ptr(output_buffer.data());

    trans.set_data_length(output_bytes);

    trans.set_streaming_width(output_bytes);

    cgra_socket->b_transport(trans, delay);

    if(trans.get_response_status()
        != TLM_OK_RESPONSE)
    {
        SC_REPORT_ERROR(
            "CSR_DMA",
            "Failed to receive the CGRA output data.");
    }

}

void CSR_DMA::write_results() {
    sc_time delay = SC_ZERO_TIME;

    tlm_generic_payload trans;


    const uint32_t output_bytes = (cgra_config == VECTOR_ADD || cgra_config == FULL_PIPELINE) ? 16 : data_size;

    trans.set_command(TLM_WRITE_COMMAND);

    trans.set_address(output_addr);

    trans.set_data_ptr(output_buffer.data());

    trans.set_data_length(output_bytes);

    trans.set_streaming_width(output_bytes);

    memory_socket->b_transport(trans, delay);

    if(trans.get_response_status()
        != TLM_OK_RESPONSE)
    {
        SC_REPORT_ERROR(
            "CSR_DMA",
            "Failed to write the results into Main Memory.");
    }

}