// ALU_vector.h
// ALU vectorial: aritmetica/logica RV32I completa por lanes, mas MUL.
// Puramente combinacional, sin estado interno.

#ifndef ALU_VECTOR_H
#define ALU_VECTOR_H

#include <systemc.h>
#include "../pe_isa.h"

template <int DATA_W = 32, int VLEN = 4>
class ALU_vector : public sc_core::sc_module {
public:
    sc_in<sc_uint<4>>              opcode;
    sc_in<PE_VectorData<DATA_W, VLEN>> operand_a;
    sc_in<PE_VectorData<DATA_W, VLEN>> operand_b;
    sc_in<bool>                    enable;

    // Disparador de despacho. La PE lo invierte cada vez que despacha una
    // instruccion, y compute() es sensible a el ADEMAS de a los operandos.
    //
    // Por que hace falta: sc_signal::write() no genera evento cuando el valor nuevo
    // coincide con el actual. Dos instrucciones CONSECUTIVAS pueden coincidir en
    // (opcode, operand_a, operand_b) aunque escriban a registros destino distintos --
    // por ejemplo "T2 = SRA(T1,12)" seguida de "T6 = SRA(T2,12)" cuando T1 == T2 == 0.
    // Sin este puerto, issue() no genera ningun evento, compute() no vuelve a correr,
    // valid_toggle no cambia, writeback() no dispara y el registro destino de la
    // SEGUNDA instruccion se queda con el valor que tuviera de antes: un resultado
    // silenciosamente incorrecto que depende de los datos, no del programa.
    //
    // Es el mismo problema que valid_toggle ya resuelve entre esta ALU y writeback(),
    // pero una etapa antes, entre issue() y esta ALU.
    sc_in<bool>            issue_toggle;

    sc_out<PE_VectorData<DATA_W, VLEN>> result;
    sc_out<bool>                   valid;
    // Se invierte cada vez que compute() produce un resultado habilitado, sin
    // importar si el valor coincide con el anterior. sc_signal::write() no genera
    // evento cuando el valor nuevo es igual al actual, asi que un consumidor
    // sensible solo a result/valid puede perderse una escritura (p.ej. un
    // acumulador donde dos resultados consecutivos coinciden). valid_toggle
    // siempre cambia, por construccion, y sirve como disparador confiable.
    sc_out<bool>                   valid_toggle;

    SC_HAS_PROCESS(ALU_vector);

    explicit ALU_vector(sc_core::sc_module_name name)
        : sc_module(name),
          opcode("opcode"), operand_a("operand_a"), operand_b("operand_b"),
          enable("enable"), issue_toggle("issue_toggle"),
          result("result"), valid("valid"),
          valid_toggle("valid_toggle")
    {
        SC_METHOD(compute);
        sensitive << opcode << operand_a << operand_b << enable << issue_toggle;
    }

private:
    void compute() {
        if (!enable.read()) {
            valid.write(false);
            return;
        }

        PE_VectorData<DATA_W, VLEN> a = operand_a.read();
        PE_VectorData<DATA_W, VLEN> b = operand_b.read();
        PE_VectorData<DATA_W, VLEN> r;

        for (int i = 0; i < VLEN; ++i) {
            unsigned shamt = b[i].to_uint() & (DATA_W - 1);
            switch (opcode.read()) {
                case OP_ADD:  r[i] = a[i] + b[i]; break;
                case OP_SUB:  r[i] = a[i] - b[i]; break;
                case OP_AND:  r[i] = a[i] & b[i]; break;
                case OP_OR:   r[i] = a[i] | b[i]; break;
                case OP_XOR:  r[i] = a[i] ^ b[i]; break;
                case OP_MOV:  r[i] = a[i]; break;
                case OP_SLL:  r[i] = a[i] << shamt; break;
                case OP_SRL:  r[i] = sc_int<DATA_W>(sc_uint<DATA_W>(a[i]) >> shamt); break;
                case OP_SRA:  r[i] = a[i] >> shamt; break;
                case OP_SLT:  r[i] = (a[i] < b[i]) ? 1 : 0; break;
                case OP_SLTU: r[i] = (sc_uint<DATA_W>(a[i]) < sc_uint<DATA_W>(b[i])) ? 1 : 0; break;
                case OP_MUL:  r[i] = a[i] * b[i]; break;
                case OP_NOP:
                default:      r[i] = 0; break;
            }
        }

        result.write(r);
        valid.write(true);
        valid_toggle.write(!valid_toggle.read());
    }
};

#endif // ALU_VECTOR_H
