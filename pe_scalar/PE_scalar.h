// PE_scalar.h
// Processing Element escalar: top del diseño. Puertos de malla N/S/E/W (4
// entradas + 4 salidas de datos, uno por vecino), 1 entrada de instrucciones
// (carga de programa), clk/rst/enable. Memoria de instrucciones interna +
// logica de control (PC/issue/writeback) + banco de registros. La ALU es un
// modulo aparte (ALU_scalar), instanciado y cableado aqui con señales internas.

#ifndef PE_SCALAR_H
#define PE_SCALAR_H

#include <systemc.h>
#include "pe_isa.h"
#include "ALU_scalar.h"

template <int DATA_W = 32, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_scalar : public sc_core::sc_module {
public:
    typedef PE_Instruction<DATA_W> Instr;
    typedef PE_InstrIn<DATA_W>     InstrIn;

    // ---- Control ------------------------------------------------------
    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    // ---- Datos: un puerto de entrada y uno de salida por vecino de malla ---
    sc_in<sc_int<DATA_W>>  in_N, in_S, in_E, in_W;
    sc_out<sc_int<DATA_W>> out_N, out_S, out_E, out_W;

    // ---- Carga de programa ------------------------------------------------
    sc_in<InstrIn> instr_in;

    // ---- ALU --------------------------------------------------------------
    ALU_scalar<DATA_W> alu;

    SC_HAS_PROCESS(PE_scalar);

    explicit PE_scalar(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          instr_in("instr_in"),
          alu("alu"),
          pc(0)
    {
        for (int i = 0; i < NUM_REGS; i++) reg_file[i] = 0;
        for (int i = 0; i < INSTR_MEM_SIZE; i++) instr_mem[i] = Instr();

        alu.opcode(sig_alu_opcode);
        alu.operand_a(sig_alu_a);
        alu.operand_b(sig_alu_b);
        alu.enable(sig_alu_enable);
        alu.result(sig_alu_result);
        alu.valid(sig_alu_valid);

        SC_METHOD(load_program);
        sensitive << clk.pos();

        SC_METHOD(pc_update);
        sensitive << clk.pos();

        SC_METHOD(issue);
        sensitive << clk.pos();

        SC_METHOD(writeback);
        sensitive << sig_alu_valid << sig_alu_result;
    }

    // -----------------------------------------------------------------------
    // Traza el estado interno (no los puertos, esos los traza quien posea las
    // señales externas) para poder inspeccionarlo en un waveform VCD.
    // -----------------------------------------------------------------------
    void trace(sc_core::sc_trace_file* tf) const {
        std::string p = name();

        sc_trace(tf, pc,        p + ".pc");
        sc_trace(tf, cur_instr, p + ".cur_instr");

        for (int i = 0; i < NUM_REGS; i++) {
            sc_trace(tf, reg_file[i], p + ".reg_file_" + std::to_string(i));
        }

        sc_trace(tf, sig_alu_opcode, p + ".alu_opcode");
        sc_trace(tf, sig_alu_a,      p + ".alu_a");
        sc_trace(tf, sig_alu_b,      p + ".alu_b");
        sc_trace(tf, sig_alu_result, p + ".alu_result");
        sc_trace(tf, sig_alu_enable, p + ".alu_enable");
        sc_trace(tf, sig_alu_valid,  p + ".alu_valid");
    }

private:
    // ---- Estado interno -----------------------------------------------------
    Instr          instr_mem[INSTR_MEM_SIZE];
    sc_int<DATA_W> reg_file[NUM_REGS];
    sc_uint<16>    pc;
    Instr          cur_instr;   // instruccion en curso (latched en issue)

    // señales internas hacia la ALU
    sc_signal<sc_uint<4>>     sig_alu_opcode;
    sc_signal<sc_int<DATA_W>> sig_alu_a, sig_alu_b, sig_alu_result;
    sc_signal<bool>           sig_alu_enable, sig_alu_valid;

    // -----------------------------------------------------------------------
    // Carga de configuracion: escribe una instruccion en la memoria interna.
    // Congelada junto con el resto de la PE cuando enable=0.
    // -----------------------------------------------------------------------
    void load_program() {
        if (rst.read() || !enable.read()) return;

        InstrIn load = instr_in.read();
        if (load.valid) {
            int addr = load.addr.to_uint() % INSTR_MEM_SIZE;
            instr_mem[addr] = load.instr;
        }
    }

    // -----------------------------------------------------------------------
    // Actualizacion de PC: recorre en bucle la memoria de instrucciones.
    // -----------------------------------------------------------------------
    void pc_update() {
        if (rst.read()) {
            pc = 0;
            return;
        }
        if (enable.read()) {
            pc = (pc + 1) % INSTR_MEM_SIZE;
        }
    }

    // -----------------------------------------------------------------------
    // Seleccion de un operando de entrada segun PE_Src.
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // Fetch + seleccion de operandos + emision hacia la ALU. Congelada
    // (sin refetch) cuando rst o enable=0.
    // -----------------------------------------------------------------------
    void issue() {
        if (rst.read() || !enable.read()) {
            sig_alu_enable.write(false);
            return;
        }

        Instr ins = instr_mem[pc.to_uint() % INSTR_MEM_SIZE];
        cur_instr = ins;

        sc_int<DATA_W> a = select_src(ins.src_a, ins.reg_a, ins.imm);
        sc_int<DATA_W> b = select_src(ins.src_b, ins.reg_b, ins.imm);

        sig_alu_opcode.write(ins.opcode);
        sig_alu_a.write(a);
        sig_alu_b.write(b);
        sig_alu_enable.write(ins.opcode != OP_NOP);
    }

    // -----------------------------------------------------------------------
    // Escritura del resultado en registro o en el/los puerto(s) de salida,
    // segun el campo 'dst' de la instruccion en curso. Los puertos de salida
    // que no aplican no se reescriben y retienen su ultimo valor (salida
    // registrada).
    // -----------------------------------------------------------------------
    void writeback() {
        if (!sig_alu_valid.read()) return;

        sc_int<DATA_W> r = sig_alu_result.read();

        switch (cur_instr.dst) {
            case DST_REG:   reg_file[cur_instr.reg_dst.to_uint() % NUM_REGS] = r; break;
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

#endif // PE_SCALAR_H
