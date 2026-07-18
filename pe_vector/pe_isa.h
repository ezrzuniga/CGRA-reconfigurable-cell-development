// pe_isa.h
// ISA vectorial de la PE: misma semántica de instrucciones que el diseño
// escalar, pero operando sobre vectores de lanes paralelos.

#ifndef PE_VECTOR_ISA_H
#define PE_VECTOR_ISA_H

#include <systemc.h>
#include <array>

// Mismos enums que pe_scalar/pe_isa.h (opcodes/src/dst identicos). Si esa cabecera
// ya fue incluida en esta unidad de traduccion (p.ej. desde cgra_mesh/PE_Base.h, que
// necesita ambos pe_isa.h a la vez), no se redeclaran para evitar un choque de tipos.
#ifndef PE_ISA_H

// Opcodes: aritmética/logica entera completa estilo RV32I (shifts y
// comparaciones con/sin signo incluidas) mas MUL, ademas de NOP y MOV.
enum PE_Opcode {
    OP_NOP  = 0,
    OP_ADD  = 1,
    OP_SUB  = 2,
    OP_AND  = 3,
    OP_OR   = 4,
    OP_XOR  = 5,
    OP_MOV  = 6,
    OP_SLL  = 7,
    OP_SRL  = 8,
    OP_SRA  = 9,
    OP_SLT  = 10,
    OP_SLTU = 11,
    OP_MUL  = 12
};

// Origen de un operando: registro interno, uno de los 4 vecinos de malla de
// la PE, o inmediato embebido en la instruccion.
enum PE_Src {
    SRC_REG   = 0,
    SRC_NORTH = 1,
    SRC_SOUTH = 2,
    SRC_EAST  = 3,
    SRC_WEST  = 4,
    SRC_IMM   = 5
};

// Destino del resultado: registro interno, un vecino de malla especifico, o
// difusion (ALL) a los 4 vecinos a la vez.
enum PE_Dst {
    DST_REG   = 0,
    DST_NORTH = 1,
    DST_SOUTH = 2,
    DST_EAST  = 3,
    DST_WEST  = 4,
    DST_ALL   = 5
};

#endif // PE_ISA_H

template <int DATA_W = 32, int VLEN = 4>
struct PE_VectorData {
    std::array<sc_int<DATA_W>, VLEN> lane;

    PE_VectorData() {
        lane.fill(0);
    }

    PE_VectorData(const std::array<sc_int<DATA_W>, VLEN>& init)
        : lane(init) {}

    PE_VectorData(std::initializer_list<sc_int<DATA_W>> init) {
        lane.fill(0);
        int i = 0;
        for (auto value : init) {
            if (i >= VLEN) break;
            lane[i++] = value;
        }
    }

    sc_int<DATA_W>& operator[](size_t idx) {
        return lane[idx];
    }

    const sc_int<DATA_W>& operator[](size_t idx) const {
        return lane[idx];
    }

    bool operator==(const PE_VectorData<DATA_W, VLEN>& other) const {
        return lane == other.lane;
    }

    bool operator!=(const PE_VectorData<DATA_W, VLEN>& other) const {
        return !(*this == other);
    }
};

template <int DATA_W, int VLEN>
inline ostream& operator<<(ostream& os, const PE_VectorData<DATA_W, VLEN>& vec) {
    os << "[";
    for (int i = 0; i < VLEN; ++i) {
        os << vec.lane[i];
        if (i + 1 < VLEN) os << ", ";
    }
    os << "]";
    return os;
}

template <int DATA_W, int VLEN>
inline void sc_trace(sc_core::sc_trace_file* tf, const PE_VectorData<DATA_W, VLEN>& vec, const std::string& name) {
    for (int i = 0; i < VLEN; ++i) {
        sc_trace(tf, vec.lane[i], name + ".lane" + std::to_string(i));
    }
}

// Nombre distinto de PE_Instruction/PE_InstrIn (pe_scalar/pe_isa.h) a proposito: son
// estructuralmente identicos salvo el parametro VLEN (que ni siquiera se usa en el
// cuerpo, imm sigue escalar), pero dos templates de clase con el mismo nombre y
// distinta cantidad de parametros no pueden coexistir en la misma unidad de
// traduccion — y cgra_mesh/PE_Base.h necesita incluir ambos pe_isa.h a la vez.
template <int DATA_W = 32, int VLEN = 4>
struct PE_VecInstruction {
    sc_uint<4> opcode;
    sc_uint<3> src_a;
    sc_uint<3> src_b;
    sc_uint<3> dst;
    sc_uint<5> reg_a;
    sc_uint<5> reg_b;
    sc_uint<5> reg_dst;
    sc_int<DATA_W> imm;

    PE_VecInstruction()
        : opcode(OP_NOP), src_a(SRC_REG), src_b(SRC_REG), dst(DST_REG),
          reg_a(0), reg_b(0), reg_dst(0), imm(0) {}

    inline bool operator==(const PE_VecInstruction<DATA_W, VLEN>& o) const {
        return opcode == o.opcode && src_a == o.src_a && src_b == o.src_b &&
               dst == o.dst && reg_a == o.reg_a && reg_b == o.reg_b &&
               reg_dst == o.reg_dst && imm == o.imm;
    }
};

template <int DATA_W, int VLEN>
inline ostream& operator<<(ostream& os, const PE_VecInstruction<DATA_W, VLEN>& instr) {
    os << "{op=" << instr.opcode << "}";
    return os;
}

template <int DATA_W, int VLEN>
inline void sc_trace(sc_core::sc_trace_file* tf, const PE_VecInstruction<DATA_W, VLEN>& instr, const std::string& name) {
    sc_trace(tf, instr.opcode,  name + ".opcode");
    sc_trace(tf, instr.src_a,   name + ".src_a");
    sc_trace(tf, instr.src_b,   name + ".src_b");
    sc_trace(tf, instr.dst,     name + ".dst");
    sc_trace(tf, instr.reg_a,   name + ".reg_a");
    sc_trace(tf, instr.reg_b,   name + ".reg_b");
    sc_trace(tf, instr.reg_dst, name + ".reg_dst");
    sc_trace(tf, instr.imm,     name + ".imm");
}

template <int DATA_W = 32, int VLEN = 4>
struct PE_VecInstrIn {
    bool valid;
    sc_uint<8> addr;
    PE_VecInstruction<DATA_W, VLEN> instr;

    PE_VecInstrIn() : valid(false), addr(0), instr() {}

    inline bool operator==(const PE_VecInstrIn<DATA_W, VLEN>& o) const {
        return valid == o.valid && addr == o.addr && instr == o.instr;
    }
};

template <int DATA_W, int VLEN>
inline ostream& operator<<(ostream& os, const PE_VecInstrIn<DATA_W, VLEN>& in) {
    os << "{valid=" << in.valid << ", addr=" << in.addr << ", instr=" << in.instr << "}";
    return os;
}

template <int DATA_W, int VLEN>
inline void sc_trace(sc_core::sc_trace_file* tf, const PE_VecInstrIn<DATA_W, VLEN>& in, const std::string& name) {
    sc_trace(tf, in.valid, name + ".valid");
    sc_trace(tf, in.addr,  name + ".addr");
    sc_trace(tf, in.instr, name + ".instr");
}

#endif // PE_VECTOR_ISA_H
