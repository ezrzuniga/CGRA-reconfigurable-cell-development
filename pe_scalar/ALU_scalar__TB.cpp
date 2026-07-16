// ALU_scalar__TB.cpp
// Testbench dedicado a ALU_scalar (sin PE_scalar/memoria de instrucciones):
// corre un vector de prueba por cada opcode, incluyendo casos limite de
// shifts (enmascarado del monto), SLT vs SLTU (signed/unsigned), y overflow
// de MUL.

#include <systemc.h>
#include "ALU_scalar.h"

struct TestCase {
    const char* name;
    int opcode;
    int32_t a;
    int32_t b;
    int32_t expected;
};

int sc_main(int argc, char* argv[]) {
    sc_signal<sc_uint<4>>  opcode;
    sc_signal<sc_int<32>>  operand_a, operand_b, result;
    sc_signal<bool>        enable, valid;

    ALU_scalar<32> alu("alu");
    alu.opcode(opcode);
    alu.operand_a(operand_a);
    alu.operand_b(operand_b);
    alu.enable(enable);
    alu.result(result);
    alu.valid(valid);

    enable.write(true);

    TestCase cases[] = {
        {"NOP",  OP_NOP,  123,   456,  0},
        {"ADD",  OP_ADD,    5,     3,  8},
        {"SUB",  OP_SUB,    5,     3,  2},
        {"AND",  OP_AND,  0xF0F0, 0x0FF0, 0x00F0},
        {"OR",   OP_OR,   0xF000, 0x000F, 0xF00F},
        {"XOR",  OP_XOR,  0xFF,   0x0F,   0xF0},
        {"MOV",  OP_MOV,  42,    999,  42},
        {"SLL",  OP_SLL,  1,       4,  16},
        {"SLL_mask", OP_SLL, 1,   33,  2},   // shamt = 33 & 31 = 1
        {"SRL",  OP_SRL,  -8,      1,  2147483644}, // logico: rellena con 0
        {"SRA",  OP_SRA,  -8,      1,  -4},          // aritmetico: preserva signo
        {"SLT",  OP_SLT,  -1,      1,  1},   // con signo: -1 < 1
        {"SLTU", OP_SLTU, -1,      1,  0},   // sin signo: 0xFFFFFFFF < 1 es falso
        {"MUL",  OP_MUL,  65536, 65536, 0},  // 2^32 truncado a 32 bits
    };

    bool all_pass = true;
    for (const auto& tc : cases) {
        opcode.write(tc.opcode);
        operand_a.write(tc.a);
        operand_b.write(tc.b);
        sc_start(1, SC_NS);

        int32_t got = result.read().to_int();
        if (got == tc.expected) {
            cout << "PASS " << tc.name << " (a=" << tc.a << ", b=" << tc.b
                 << ") -> " << got << endl;
        } else {
            cout << "FAIL " << tc.name << " (a=" << tc.a << ", b=" << tc.b
                 << ") -> got " << got << ", expected " << tc.expected << endl;
            all_pass = false;
        }
    }

    return all_pass ? 0 : 1;
}
