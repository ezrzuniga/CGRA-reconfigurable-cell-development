#ifndef ACCESS_CONTROLLER_H
#define ACCESS_CONTROLLER_H

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

#endif // ACCESS_CONTROLLER_H
