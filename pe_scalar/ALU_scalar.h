// ALU_scalar.h
// ALU entera: aritmetica/logica RV32I completa (shifts, comparaciones
// con/sin signo) mas MUL, sin estado interno. Puramente combinacional (no
// necesita clk/rst).

#ifndef ALU_SCALAR_H
#define ALU_SCALAR_H

#include <systemc.h>
#include "pe_isa.h"

template <int DATA_W = 32>
class ALU_scalar : public sc_core::sc_module {
public:
    sc_in<sc_uint<4>>      opcode;
    sc_in<sc_int<DATA_W>>  operand_a;
    sc_in<sc_int<DATA_W>>  operand_b;
    sc_in<bool>            enable;

    sc_out<sc_int<DATA_W>> result;
    sc_out<bool>           valid;
    // Se invierte cada vez que compute() produce un resultado habilitado, sin
    // importar si el valor coincide con el anterior. sc_signal::write() no genera
    // evento cuando el valor nuevo es igual al actual, asi que un consumidor
    // sensible solo a result/valid puede perderse una escritura (p.ej. un
    // acumulador donde dos resultados consecutivos coinciden). valid_toggle
    // siempre cambia, por construccion, y sirve como disparador confiable.
    sc_out<bool>           valid_toggle;

    SC_HAS_PROCESS(ALU_scalar);

    explicit ALU_scalar(sc_core::sc_module_name name)
        : sc_module(name),
          opcode("opcode"), operand_a("operand_a"), operand_b("operand_b"),
          enable("enable"), result("result"), valid("valid"),
          valid_toggle("valid_toggle")
    {
        SC_METHOD(compute);
        sensitive << opcode << operand_a << operand_b << enable;
    }

private:
    void compute() {
        if (!enable.read()) {
            valid.write(false);
            return;
        }

        sc_int<DATA_W> a = operand_a.read();
        sc_int<DATA_W> b = operand_b.read();
        sc_int<DATA_W> r = 0;
        unsigned shamt = b.to_uint() & (DATA_W - 1);

        switch (opcode.read()) {
            case OP_ADD:  r = a + b; break;
            case OP_SUB:  r = a - b; break;
            case OP_AND:  r = a & b; break;
            case OP_OR:   r = a | b; break;
            case OP_XOR:  r = a ^ b; break;
            case OP_MOV:  r = a;     break;
            case OP_SLL:  r = a << shamt; break;
            case OP_SRL:  r = sc_int<DATA_W>(sc_uint<DATA_W>(a) >> shamt); break;
            case OP_SRA:  r = a >> shamt; break;
            case OP_SLT:  r = (a < b) ? 1 : 0; break;
            case OP_SLTU: r = (sc_uint<DATA_W>(a) < sc_uint<DATA_W>(b)) ? 1 : 0; break;
            case OP_MUL:  r = a * b; break;
            case OP_NOP:
            default:      r = 0;     break;
        }

        result.write(r);
        valid.write(true);
        valid_toggle.write(!valid_toggle.read());
    }
};

#endif // ALU_SCALAR_H
