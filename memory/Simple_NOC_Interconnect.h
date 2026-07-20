#ifndef SIMPLE_NOC_INTERCONNECT_H
#define SIMPLE_NOC_INTERCONNECT_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <array>
#include <cstdint>

using namespace sc_core;
using namespace tlm;

class SimpleNoCInterconnect : public sc_module {
public:
    static constexpr int NUM_TILES = 2;

    tlm_utils::simple_target_socket<SimpleNoCInterconnect> target_sockets[NUM_TILES];
    tlm_utils::simple_initiator_socket<SimpleNoCInterconnect> initiator_sockets[NUM_TILES];

    SC_CTOR(SimpleNoCInterconnect)
        : target_sockets{tlm_utils::simple_target_socket<SimpleNoCInterconnect>("target_socket_0"),
                         tlm_utils::simple_target_socket<SimpleNoCInterconnect>("target_socket_1")},
          initiator_sockets{tlm_utils::simple_initiator_socket<SimpleNoCInterconnect>("initiator_socket_0"),
                            tlm_utils::simple_initiator_socket<SimpleNoCInterconnect>("initiator_socket_1")}
    {
        target_sockets[0].register_b_transport(this, &SimpleNoCInterconnect::b_transport_0);
        target_sockets[1].register_b_transport(this, &SimpleNoCInterconnect::b_transport_1);
    }

private:
    void b_transport_0(tlm_generic_payload& trans, sc_time& delay) {
        forward_transaction(0, trans, delay);
    }

    void b_transport_1(tlm_generic_payload& trans, sc_time& delay) {
        forward_transaction(1, trans, delay);
    }

    void forward_transaction(int src_tile, tlm_generic_payload& trans, sc_time& delay) {
        const uint64_t addr = trans.get_address();
        const int dst_tile = select_destination_tile(addr);

        if (dst_tile < 0 || dst_tile >= NUM_TILES) {
            trans.set_response_status(TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        initiator_sockets[dst_tile]->b_transport(trans, delay);
    }

    int select_destination_tile(uint64_t addr) const {
        // Route traffic to the second tile for addresses in the upper half of the
        // 2 KiB SRAM window; otherwise keep it local to the first tile.
        return (addr >> 8) & 0x1;
    }
};

#endif // SIMPLE_NOC_INTERCONNECT_H
