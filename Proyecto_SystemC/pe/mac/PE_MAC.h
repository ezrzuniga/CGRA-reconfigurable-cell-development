// PE_MAC.h
// Processing Element de multiply-accumulate vectorial de 1 ciclo. Misma
// estructura que pe/vector/PE_vector.h (fetch/issue/writeback, banco de
// registros, puertos de malla), mas un acumulador interno `acc` (uno por
// lane) que OP_MAC actualiza y expone en el mismo ciclo: acc = acc + a*b,
// y el valor escrito a `dst` es el acumulador ya actualizado.
//
// El acumulador es direccionable via SRC_ACC/DST_ACC (reusando OP_MOV para
// leerlo sin destruirlo o para precargarlo/limpiarlo), no solo por PC/rst:
// necesario para que un PE pueda reiniciar su propia acumulacion (tiles de
// GEMM, etapas de FFT) sin depender de `rst`, que es un solo dominio para
// toda la malla.
//
// A proposito, `rst` NO limpia `acc` -- mismo precedente que reg_file, que
// tampoco se limpia con rst hoy en PE_scalar/PE_vector. Solo DST_ACC limpia
// o precarga el acumulador.

#ifndef PE_MAC_H
#define PE_MAC_H

#include <systemc.h>
#include "../pe_isa.h"
#include "ALU_MAC.h"

template <int DATA_W = 32, int VLEN = 4, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_MAC : public sc_core::sc_module {
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

    ALU_MAC<DATA_W, VLEN> alu;

    SC_HAS_PROCESS(PE_MAC);

    explicit PE_MAC(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          instr_in("instr_in"), alu("alu"), pc(0), acc()
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

        // Distinto de PE_vector::writeback() (sensible a valid_toggle): con
        // OP_MAC la misma instruccion se repite con los mismos operandos
        // ciclo a ciclo (in_W/imm constantes), asi que issue() escribe el
        // mismo valor a las senales del ALU cada vez -- sc_signal::write() no
        // genera evento si el valor no cambia, entonces compute() (y por lo
        // tanto valid_toggle) solo se dispara la primera vez, nunca de nuevo,
        // y acc dejaria de acumular. writeback() sensible a clk.neg() (no
        // clk.pos() ni valid_toggle) corre todos los ciclos sin depender de
        // que la ALU haya recalculado algo nuevo, y al caer en la mitad baja
        // del periodo siempre lee cur_instr/sig_alu_result/sig_alu_valid ya
        // asentados desde el flanco de subida de ese mismo ciclo (issue() +
        // ALU_MAC::compute() corren y se estabilizan en el mismo timestep,
        // antes de que avance el tiempo simulado) -- evita leer datos de
        // transicion mezclados con el ciclo anterior.
        SC_METHOD(writeback);
        sensitive << clk.neg();
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
        for (int j = 0; j < VLEN; ++j) {
            sc_trace(tf, acc.lane[j], p + ".acc.lane" + std::to_string(j));
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
    VectorData acc;

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
            case SRC_ACC:   return acc;
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

        if (cur_instr.opcode == OP_MAC) {
            for (int i = 0; i < VLEN; ++i) acc[i] = acc[i] + r[i];
            r = acc;
        } else if (cur_instr.dst == DST_ACC) {
            acc = r;
        }

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
            case DST_ACC: break; // ya resuelto arriba
            default: break;
        }
    }
};

#endif // PE_MAC_H
