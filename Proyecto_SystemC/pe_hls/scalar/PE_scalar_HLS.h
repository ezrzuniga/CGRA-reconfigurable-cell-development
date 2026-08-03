// PE_scalar_HLS.h
// Version HLS-compatible de pe/scalar/PE_scalar.h: mismo ISA, mismo banco de
// registros y memoria de instrucciones, pero fetch+issue+ALU+writeback
// colapsados en un unico SC_METHOD sensible a clk.pos(). La ALU original
// era un sc_module aparte cableado por sc_signal, con writeback() disparado
// por un truco de semantica de simulador (valid_toggle) porque
// sc_signal::write() no genera evento cuando el valor nuevo es igual al
// actual -- aca no aplica: el resultado de la ALU es una variable local que
// se consume en el mismo ciclo, no hay senal intermedia que pueda quedar
// "sin evento".

#ifndef PE_SCALAR_HLS_H
#define PE_SCALAR_HLS_H

#include <systemc.h>
#include "../../pe/pe_isa.h"

template <int DATA_W = 32, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_scalar_HLS : public sc_core::sc_module {
public:
    typedef PE_Instruction<DATA_W> Instr;
    typedef PE_InstrIn<DATA_W>     InstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_in<sc_int<DATA_W>>  in_N, in_S, in_E, in_W;
    sc_out<sc_int<DATA_W>> out_N, out_S, out_E, out_W;

    sc_in<InstrIn> instr_in;

    SC_HAS_PROCESS(PE_scalar_HLS);

    explicit PE_scalar_HLS(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          instr_in("instr_in"),
          pc(0)
    {
        for (int i = 0; i < NUM_REGS; i++) reg_file[i] = 0;
        for (int i = 0; i < INSTR_MEM_SIZE; i++) instr_mem[i] = Instr();

        SC_METHOD(tick);
        sensitive << clk.pos();
    }

    void trace(sc_core::sc_trace_file* tf) const {
        std::string p = name();
        sc_trace(tf, pc,        p + ".pc");
        sc_trace(tf, cur_instr, p + ".cur_instr");
        for (int i = 0; i < NUM_REGS; i++) {
            sc_trace(tf, reg_file[i], p + ".reg_file_" + std::to_string(i));
        }
    }

private:
    Instr          instr_mem[INSTR_MEM_SIZE];
    sc_int<DATA_W> reg_file[NUM_REGS];
    sc_uint<16>    pc;
    Instr          cur_instr;

    sc_int<DATA_W> select_src(sc_uint<3> sel, sc_uint<5> reg_idx, sc_int<DATA_W> imm) {
        switch (sel) {
            case SRC_REG:   return reg_file[reg_idx.to_uint() % NUM_REGS];
            case SRC_NORTH: return in_N.read();
            case SRC_SOUTH: return in_S.read();
            case SRC_EAST:  return in_E.read();
            case SRC_WEST:  return in_W.read();
            case SRC_IMM:   return imm;
            default:        return 0;
        }
    }

    sc_int<DATA_W> alu_compute(sc_uint<4> opcode, sc_int<DATA_W> a, sc_int<DATA_W> b) {
        unsigned shamt = b.to_uint() & (DATA_W - 1);
        switch (opcode) {
            case OP_ADD:  return a + b;
            case OP_SUB:  return a - b;
            case OP_AND:  return a & b;
            case OP_OR:   return a | b;
            case OP_XOR:  return a ^ b;
            case OP_MOV:  return a;
            case OP_SLL:  return a << shamt;
            case OP_SRL:  return sc_int<DATA_W>(sc_uint<DATA_W>(a) >> shamt);
            case OP_SRA:  return a >> shamt;
            case OP_SLT:  return (a < b) ? 1 : 0;
            case OP_SLTU: return (sc_uint<DATA_W>(a) < sc_uint<DATA_W>(b)) ? 1 : 0;
            case OP_MUL:  return a * b;
            default:      return 0;
        }
    }

    void writeback(const Instr& ins, sc_int<DATA_W> r) {
        switch (ins.dst) {
            case DST_REG:   reg_file[ins.reg_dst.to_uint() % NUM_REGS] = r; break;
            case DST_NORTH: out_N.write(r); break;
            case DST_SOUTH: out_S.write(r); break;
            case DST_EAST:  out_E.write(r); break;
            case DST_WEST:  out_W.write(r); break;
            case DST_ALL:
                out_N.write(r); out_S.write(r);
                out_E.write(r); out_W.write(r);
                break;
            default: break;
        }
    }

    void tick() {
        if (rst.read()) {
            pc = 0;
            return;
        }
        if (!enable.read()) return;

        // Fetch antes de aplicar una carga pendiente de este mismo ciclo
        // (semantica read-old-data de BRAM de un puerto: una carga recien
        // llegada no se ejecuta en el mismo ciclo en que se programa, recien
        // queda visible para el fetch del ciclo siguiente).
        Instr ins = instr_mem[pc.to_uint() % INSTR_MEM_SIZE];
        cur_instr = ins;

        InstrIn load = instr_in.read();
        if (load.valid) {
            int addr = load.addr.to_uint() % INSTR_MEM_SIZE;
            instr_mem[addr] = load.instr;
        }

        sc_int<DATA_W> a = select_src(ins.src_a, ins.reg_a, ins.imm);
        sc_int<DATA_W> b = select_src(ins.src_b, ins.reg_b, ins.imm);
        sc_int<DATA_W> r = alu_compute(ins.opcode, a, b);

        if (ins.opcode != OP_NOP) writeback(ins, r);

        pc = (pc + 1) % INSTR_MEM_SIZE;
    }
};

#endif // PE_SCALAR_HLS_H
