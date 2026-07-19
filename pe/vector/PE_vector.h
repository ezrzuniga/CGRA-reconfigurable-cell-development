// PE_vector.h
// Processing Element vectorial: top del diseño. Puertos de malla N/S/E/W de
// vectores de lanes, memoria interna de instrucciones y banco de registros
// vectoriales.

#ifndef PE_VECTOR_H
#define PE_VECTOR_H

#include <systemc.h>
#include "../pe_isa.h"
#include "ALU_vector.h"

template <int DATA_W = 32, int VLEN = 4, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_vector : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN> VectorData;
    typedef PE_VecInstruction<DATA_W, VLEN> Instr;
    typedef PE_VecInstrIn<DATA_W, VLEN>     InstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_in<VectorData> in_N, in_S, in_E, in_W;
    sc_out<VectorData> out_N, out_S, out_E, out_W;

    sc_in<InstrIn> instr_in;

    ALU_vector<DATA_W, VLEN> alu;

    SC_HAS_PROCESS(PE_vector);

    explicit PE_vector(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          instr_in("instr_in"), alu("alu"), pc(0)
    {
        for (int i = 0; i < NUM_REGS; i++) reg_file[i] = VectorData();
        for (int i = 0; i < INSTR_MEM_SIZE; i++) instr_mem[i] = Instr();

        alu.opcode(sig_alu_opcode);
        alu.operand_a(sig_alu_a);
        alu.operand_b(sig_alu_b);
        alu.enable(sig_alu_enable);
        alu.result(sig_alu_result);
        alu.valid(sig_alu_valid);
        alu.valid_toggle(sig_alu_valid_toggle);

        SC_METHOD(load_program);
        sensitive << clk.pos();

        SC_METHOD(pc_update);
        sensitive << clk.pos();

        SC_METHOD(issue);
        sensitive << clk.pos();

        // Sensible solo a valid_toggle (no a sig_alu_valid/sig_alu_result
        // directamente): dos instrucciones consecutivas pueden producir el mismo
        // valor de ALU (p.ej. un acumulador que suma 0, o simplemente coincide),
        // y sc_signal no genera evento cuando el valor escrito es igual al
        // actual. valid_toggle se invierte siempre que compute() corre con
        // enable=true, sin importar el valor, asi que no se pierde ningun ciclo.
        SC_METHOD(writeback);
        sensitive << sig_alu_valid_toggle;
    }

    void trace(sc_core::sc_trace_file* tf) const {
        std::string p = name();

        sc_trace(tf, pc,        p + ".pc");
        sc_trace(tf, cur_instr, p + ".cur_instr");

        for (int i = 0; i < NUM_REGS; i++) {
            for (int j = 0; j < VLEN; ++j) {
                sc_trace(tf, reg_file[i].lane[j], p + ".reg_file_" + std::to_string(i) + ".lane" + std::to_string(j));
            }
        }

        sc_trace(tf, sig_alu_opcode, p + ".alu_opcode");
        sc_trace(tf, sig_alu_a,      p + ".alu_a");
        sc_trace(tf, sig_alu_b,      p + ".alu_b");
        sc_trace(tf, sig_alu_result, p + ".alu_result");
        sc_trace(tf, sig_alu_enable, p + ".alu_enable");
        sc_trace(tf, sig_alu_valid,  p + ".alu_valid");
        sc_trace(tf, sig_alu_valid_toggle, p + ".alu_valid_toggle");
    }

private:
    Instr instr_mem[INSTR_MEM_SIZE];
    VectorData reg_file[NUM_REGS];
    sc_uint<16> pc;
    Instr cur_instr;

    sc_signal<sc_uint<4>>      sig_alu_opcode;
    sc_signal<VectorData>      sig_alu_a, sig_alu_b, sig_alu_result;
    sc_signal<bool>            sig_alu_enable, sig_alu_valid, sig_alu_valid_toggle;

    void load_program() {
        if (rst.read() || !enable.read()) return;

        InstrIn load = instr_in.read();
        if (load.valid) {
            int addr = load.addr.to_uint() % INSTR_MEM_SIZE;
            instr_mem[addr] = load.instr;
        }
    }

    void pc_update() {
        if (rst.read()) {
            pc = 0;
            return;
        }
        if (enable.read()) {
            pc = (pc + 1) % INSTR_MEM_SIZE;
        }
    }

    VectorData select_src(sc_uint<3> sel, sc_uint<5> reg_idx, sc_int<DATA_W> imm) {
        switch (sel) {
            case SRC_REG:   return reg_file[reg_idx.to_uint() % NUM_REGS];
            case SRC_NORTH: return in_N.read();
            case SRC_SOUTH: return in_S.read();
            case SRC_EAST:  return in_E.read();
            case SRC_WEST:  return in_W.read();
            case SRC_IMM: {
                VectorData v;
                for (int i = 0; i < VLEN; ++i) v[i] = imm;
                return v;
            }
            default:        return VectorData();
        }
    }

    void issue() {
        if (rst.read() || !enable.read()) {
            sig_alu_enable.write(false);
            return;
        }

        Instr ins = instr_mem[pc.to_uint() % INSTR_MEM_SIZE];
        cur_instr = ins;

        VectorData a = select_src(ins.src_a, ins.reg_a, ins.imm);
        VectorData b = select_src(ins.src_b, ins.reg_b, ins.imm);

        sig_alu_opcode.write(ins.opcode);
        sig_alu_a.write(a);
        sig_alu_b.write(b);
        sig_alu_enable.write(ins.opcode != OP_NOP);
    }

    void writeback() {
        if (!sig_alu_valid.read()) return;

        VectorData r = sig_alu_result.read();

        switch (cur_instr.dst) {
            case DST_REG:
                reg_file[cur_instr.reg_dst.to_uint() % NUM_REGS] = r;
                break;
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
};

#endif // PE_VECTOR_H
