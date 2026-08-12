// Access_controller_C.h
// Copia local (en Proyecto_C) de memory_hls/Access_controller.h: el original
// ya era C++ puro -- sin systemc.h, sin sc_module, sin sc_int -- asi que no
// hay nada que "transliterar", solo que dejar de depender del arbol SystemC.
//
// Por que existe esta copia en vez de un #include cruzado a Proyecto_SystemC:
// el proyecto de Vitis HLS agrega los .cpp/.h por ruta relativa (ver
// Proyecto_HLS/*/run_hls.tcl) y Proyecto_C tiene que poder empaquetarse/
// sintetizarse sin arrastrar el arbol SystemC entero al include path. El
// include cruzado que habia antes en PE_Memory_HLS_C.h apuntaba ademas a una
// ruta que ya no existe (Proyecto_SystemC/memory/, hoy memory_hls/), asi que
// ningun TU que incluyera la celda de memoria compilaba.
//
// Nota de sintesis: current_index/base_addr/... son uint32_t/int32_t comunes
// (no ap_uint<N>) -- es intencional y se deja como estaba: son registros de
// control de 32 bits exactos, Vitis HLS los infiere igual, y cambiarlos a
// ap_uint<32> no cambiaria ni el area ni la semantica.

#ifndef ACCESS_CONTROLLER_C_H
#define ACCESS_CONTROLLER_C_H

#include <cstdint>

class AccessController {
public:
    enum Mode {
        MODE_DIRECT = 0,
        MODE_STRIDE = 1
    };

    AccessController() : base_addr(0), stride(0), count(0), mode(MODE_DIRECT), current_index(0) {}

    void configure(uint32_t base, int32_t strd, uint32_t cnt, uint32_t md) {
        base_addr = base;
        stride = strd;
        count = cnt;
        mode = static_cast<Mode>(md);
        current_index = 0;
    }

    void reset() {
        current_index = 0;
    }

    bool has_next() const {
        if (mode == MODE_DIRECT) {
            return current_index < 1;
        } else {
            return current_index < count;
        }
    }

    uint32_t next_address() {
        if (!has_next()) {
            return base_addr;
        }
        uint32_t addr;
        if (mode == MODE_DIRECT) {
            addr = base_addr;
        } else {
            addr = base_addr + current_index * stride;
        }
        current_index++;
        return addr;
    }

    uint32_t get_current_index() const {
        return current_index;
    }

private:
    uint32_t base_addr;
    int32_t stride;
    uint32_t count;
    Mode mode;
    uint32_t current_index;
};

#endif // ACCESS_CONTROLLER_C_H
