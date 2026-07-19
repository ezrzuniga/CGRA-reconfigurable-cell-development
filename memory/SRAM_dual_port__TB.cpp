#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include "SRAM_dual_port.h"

using namespace sc_core;
using namespace tlm;

// Simple writer module
SC_MODULE(WriterInitiator) {
    tlm_utils::simple_initiator_socket<WriterInitiator> socket;

    SC_CTOR(WriterInitiator) : socket("socket") {
        SC_THREAD(run);
    }

    void run() {
        wait(10, SC_NS); // Start after reset

        for (int i = 0; i < 8; ++i) {
            uint32_t val = 100 + i;
            tlm_generic_payload trans;
            sc_time delay = SC_ZERO_TIME;

            trans.set_command(TLM_WRITE_COMMAND);
            trans.set_address(i * 4);
            trans.set_data_ptr(reinterpret_cast<unsigned char*>(&val));
            trans.set_data_length(4);
            trans.set_streaming_width(4);

            cout << "@" << sc_time_stamp() << " [Writer] Writing " << val << " to address " << i * 4 << endl;
            socket->b_transport(trans, delay);
            wait(delay);
            wait(10, SC_NS); // Cycle spacing
        }
    }
};

// Simple reader module
SC_MODULE(ReaderInitiator) {
    tlm_utils::simple_initiator_socket<ReaderInitiator> socket;

    SC_CTOR(ReaderInitiator) : socket("socket") {
        SC_THREAD(run);
    }

    void run() {
        wait(45, SC_NS); // Start reading while writer is in the middle of writing

        for (int i = 0; i < 8; ++i) {
            uint32_t val = 0;
            tlm_generic_payload trans;
            sc_time delay = SC_ZERO_TIME;

            trans.set_command(TLM_READ_COMMAND);
            trans.set_address(i * 4);
            trans.set_data_ptr(reinterpret_cast<unsigned char*>(&val));
            trans.set_data_length(4);
            trans.set_streaming_width(4);

            socket->b_transport(trans, delay);
            wait(delay);
            cout << "@" << sc_time_stamp() << " [Reader] Read " << val << " from address " << i * 4 << endl;
            
            // Check if data is correct for addresses already written
            if (i < 3) {
                if (val != 100 + i) {
                    cout << "FAIL: expected " << 100 + i << ", got " << val << endl;
                    exit(1);
                }
            }
            wait(15, SC_NS);
        }
        cout << "SRAM DUAL PORT CONCURRENT ACCESS TB PASS!" << endl;
    }
};

int sc_main(int argc, char* argv[]) {
    SRAM_dual_port<1024> sram("sram");
    WriterInitiator writer("writer");
    ReaderInitiator reader("reader");

    writer.socket.bind(sram.target_socket_0);
    reader.socket.bind(sram.target_socket_1);

    sc_start();
    return 0;
}
