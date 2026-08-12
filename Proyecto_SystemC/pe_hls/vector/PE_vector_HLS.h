// PE_vector_HLS.h
// Version HLS-compatible de pe/vector/PE_vector.h: mismo patron que
// pe_hls/scalar/PE_scalar_HLS.h (un unico SC_METHOD por clk.pos(), ALU
// inline, sin valid_toggle), con VectorData en vez de sc_int<DATA_W> en los
// puertos de dato y el banco de registros.

#ifndef PE_VECTOR_HLS_H
#define PE_VECTOR_HLS_H

#include <systemc.h>
#include "../pe_isa.h"

template <int DATA_W = 32, int VLEN = 4, int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class PE_vector_HLS : public sc_core::sc_module {
public:
    typedef PE_VectorData<DATA_W, VLEN>     VectorData;
    typedef PE_VecInstruction<DATA_W, VLEN> Instr;
    typedef PE_VecInstrIn<DATA_W, VLEN>     InstrIn;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_in<VectorData>  in_N, in_S, in_E, in_W;
    sc_out<VectorData> out_N, out_S, out_E, out_W;

    sc_in<InstrIn> instr_in;

    SC_HAS_PROCESS(PE_vector_HLS);

    explicit PE_vector_HLS(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), in_E("in_E"), in_W("in_W"),
          out_N("out_N"), out_S("out_S"), out_E("out_E"), out_W("out_W"),
          instr_in("instr_in"),
          pc(0)
    {
        for (int i = 0; i < NUM_REGS; i++) reg_file[i] = VectorData();
        for (int i = 0; i < INSTR_MEM_SIZE; i++) instr_mem[i] = Instr();

        SC_METHOD(tick);
        sensitive << clk.pos();
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
    }

private:
    Instr      instr_mem[INSTR_MEM_SIZE];
    VectorData reg_file[NUM_REGS];
    sc_uint<16> pc;
    Instr       cur_instr;

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
            default: return VectorData();
        }
    }

    VectorData alu_compute(sc_uint<4> opcode, const VectorData& a, const VectorData& b) {
        VectorData r;
        for (int i = 0; i < VLEN; ++i) {
            unsigned shamt = b[i].to_uint() & (DATA_W - 1);
            switch (opcode) {
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
                default:      r[i] = 0; break;
            }
        }
        return r;
    }

    void writeback(const Instr& ins, const VectorData& r) {
        switch (ins.dst) {
            case DST_REG:
                reg_file[ins.reg_dst.to_uint() % NUM_REGS] = r;
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

    void tick() {
        if (rst.read()) {
            pc = 0;
            return;
        }
        if (!enable.read()) return;

        // Fetch antes de aplicar una carga pendiente de este mismo ciclo
        // (semantica read-old-data de BRAM de un puerto, ver
        // PE_scalar_HLS.h).
        Instr ins = instr_mem[pc.to_uint() % INSTR_MEM_SIZE];
        cur_instr = ins;

        InstrIn load = instr_in.read();
        if (load.valid) {
            int addr = load.addr.to_uint() % INSTR_MEM_SIZE;
            instr_mem[addr] = load.instr;
        }

        VectorData a = select_src(ins.src_a, ins.reg_a, ins.imm);
        VectorData b = select_src(ins.src_b, ins.reg_b, ins.imm);
        VectorData r = alu_compute(ins.opcode, a, b);

        if (ins.opcode != OP_NOP) writeback(ins, r);

        pc = (pc + 1) % INSTR_MEM_SIZE;
    }
};

#endif // PE_VECTOR_HLS_H
