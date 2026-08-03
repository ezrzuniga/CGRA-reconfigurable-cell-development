// ALU_MAC__TB.cpp
// Testbench dedicado a la ALU de PE_MAC: mismos vectores de prueba que
// ALU_vector__TB, mas un caso OP_MAC (debe dar el mismo resultado que
// OP_MUL, porque el += vive en PE_MAC::writeback(), no en la ALU).

#include <systemc.h>
#include "ALU_MAC.h"
#include "../pe_isa.h"

template <int DATA_W = 32, int VLEN = 4>
struct VectorTestCase {
    const char* name;
    int opcode;
    PE_VectorData<DATA_W, VLEN> a;
    PE_VectorData<DATA_W, VLEN> b;
    PE_VectorData<DATA_W, VLEN> expected;
};

int sc_main(int argc, char* argv[]) {
    sc_signal<sc_uint<4>> opcode;
    sc_signal<PE_VectorData<32, 4>> operand_a, operand_b, result;
    sc_signal<bool> enable, valid, valid_toggle;

    ALU_MAC<32, 4> alu("alu");
    alu.opcode(opcode);
    alu.operand_a(operand_a);
    alu.operand_b(operand_b);
    alu.enable(enable);
    alu.result(result);
    alu.valid(valid);
    alu.valid_toggle(valid_toggle);

    enable.write(true);

    VectorTestCase<32, 4> cases[] = {
        {"ADD",  OP_ADD,
            {{1, 2, 3, 4}}, {{5, 6, 7, 8}}, {{6, 8, 10, 12}}},
        {"SUB",  OP_SUB,
            {{10, 0, -5, -1}}, {{3, 2, 1, -2}}, {{7, -2, -6, 1}}},
        {"AND",  OP_AND,
            {{0xF0F0, 0x1234, 0xFFFF, 0x0}}, {{0x0FF0, 0x0F0F, 0x00FF, 0xFFFF}},
            {{0x00F0, 0x0204, 0x00FF, 0x0}}},
        {"OR",   OP_OR,
            {{0xF000, 0x1111, 0x8000, 0x0001}}, {{0x000F, 0xF0F0, 0x7FFF, 0xFFFE}},
            {{0xF00F, 0xF1F1, 0xFFFF, 0xFFFF}}},
        {"XOR",  OP_XOR,
            {{0xFF, 0xAAAA, 0x1234, 0xFFFF}}, {{0x0F, 0x5555, 0x0F0F, 0x00FF}},
            {{0xF0, 0xFFFF, 0x1D3B, 0xFF00}}},
        {"MOV",  OP_MOV,
            {{42, 43, -1, 7}}, {{999, 1000, 1, 2}}, {{42, 43, -1, 7}}},
        {"SLL",  OP_SLL,
            {{1, 2, 4, 8}}, {{1, 2, 3, 4}}, {{2, 8, 32, 128}}},
        {"SLL_mask", OP_SLL,
            {{1, 1, 1, 1}}, {{33, 34, 35, 36}}, {{2, 4, 8, 16}}},
        {"SRL",  OP_SRL,
            {{-8, -16, 0x7FFFFFFF, -1}}, {{1, 2, 3, 4}},
            {{2147483644, 1073741820, 268435455, 268435455}}},
        {"SRA",  OP_SRA,
            {{-8, -16, 0x7FFFFFFF, -1}}, {{1, 2, 3, 4}},
            {{-4, -4, 268435455, -1}}},
        {"SLT",  OP_SLT,
            {{-1, 0, 1, -2}}, {{1, -1, 2, -1}}, {{1, 0, 1, 1}}},
        {"SLTU", OP_SLTU,
            {{-1, 0, 1, -2}}, {{1, -1, 2, -1}}, {{0, 1, 1, 1}}},
        {"MUL",  OP_MUL,
            {{65536, 65536, 2, -1}}, {{65536, 65536, 3, 1}}, {{0, 0, 6, -1}}},
        {"MAC",  OP_MAC,
            {{65536, 65536, 2, -1}}, {{65536, 65536, 3, 1}}, {{0, 0, 6, -1}}},
    };

    bool all_pass = true;
    for (const auto& tc : cases) {
        opcode.write(tc.opcode);
        operand_a.write(tc.a);
        operand_b.write(tc.b);
        sc_start(1, SC_NS);

        PE_VectorData<32, 4> got = result.read();
        if (got == tc.expected) {
            cout << "PASS " << tc.name << " -> " << got << endl;
        } else {
            cout << "FAIL " << tc.name << " -> got " << got
                 << ", expected " << tc.expected << endl;
            all_pass = false;
        }
    }

    return all_pass ? 0 : 1;
}
