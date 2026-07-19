// pe_isa.h
// ISA compartido por las 3 variantes de PE (scalar/vector/mac): opcodes,
// formato de instruccion, y el tipo de dato vectorial. Inspirado en RISC-V
// (mismos mnemonics: ADD/SUB/AND/OR/XOR) pero adaptado para que una
// instruccion pueda operar directo sobre los 4 vecinos de malla de la PE
// (Norte/Sur/Este/Oeste) ademas del banco de registros y un inmediato.

#ifndef PE_ISA_H
#define PE_ISA_H

#include <systemc.h>
#include <array>

// Opcodes: aritmetica/logica entera completa estilo RV32I (shifts y
// comparaciones con/sin signo incluidas) mas MUL, ademas de NOP y MOV (pasa
// un operando sin modificar, util para rutar datos).
enum PE_Opcode {
    OP_NOP  = 0,
    OP_ADD  = 1,
    OP_SUB  = 2,
    OP_AND  = 3,
    OP_OR   = 4,
    OP_XOR  = 5,
    OP_MOV  = 6,
    OP_SLL  = 7,   // shift logico a la izquierda
    OP_SRL  = 8,   // shift logico a la derecha (rellena con ceros)
    OP_SRA  = 9,   // shift aritmetico a la derecha (preserva el signo)
    OP_SLT  = 10,  // 1 si a < b con signo, si no 0
    OP_SLTU = 11,  // 1 si a < b sin signo, si no 0
    OP_MUL  = 12,  // bits bajos de a * b
    OP_MAC  = 13   // acc += a * b (acumulador interno del PE, ver pe/mac/)
};

// Origen de un operando: registro interno, uno de los 4 vecinos de malla de
// la PE, inmediato embebido en la instruccion, o el acumulador interno
// (SRC_ACC, solo tiene efecto en PE_MAC).
enum PE_Src {
    SRC_REG   = 0,
    SRC_NORTH = 1,
    SRC_SOUTH = 2,
    SRC_EAST  = 3,
    SRC_WEST  = 4,
    SRC_IMM   = 5,
    SRC_ACC   = 6
};

// Destino del resultado: registro interno, un vecino de malla especifico,
// difusion (ALL) a los 4 vecinos a la vez, o el acumulador interno
// (DST_ACC, solo tiene efecto en PE_MAC).
enum PE_Dst {
    DST_REG   = 0,
    DST_NORTH = 1,
    DST_SOUTH = 2,
    DST_EAST  = 3,
    DST_WEST  = 4,
    DST_ALL   = 5,
    DST_ACC   = 6
};

// Instruccion escalar de la PE. DATA_W es el ancho de dato (por defecto 32 bits).
template <int DATA_W = 32>
struct PE_Instruction {
    sc_uint<4> opcode;
    sc_uint<3> src_a;
    sc_uint<3> src_b;
    sc_uint<3> dst;
    sc_uint<5> reg_a;
    sc_uint<5> reg_b;
    sc_uint<5> reg_dst;
    sc_int<DATA_W> imm;

    PE_Instruction()
        : opcode(OP_NOP), src_a(SRC_REG), src_b(SRC_REG), dst(DST_REG),
          reg_a(0), reg_b(0), reg_dst(0), imm(0) {}

    inline bool operator==(const PE_Instruction<DATA_W>& o) const {
        return opcode == o.opcode && src_a == o.src_a && src_b == o.src_b &&
               dst == o.dst && reg_a == o.reg_a && reg_b == o.reg_b &&
               reg_dst == o.reg_dst && imm == o.imm;
    }
};

template <int DATA_W>
inline ostream& operator<<(ostream& os, const PE_Instruction<DATA_W>& instr) {
    os << "{op=" << instr.opcode << "}";
    return os;
}

template <int DATA_W>
inline void sc_trace(sc_core::sc_trace_file* tf, const PE_Instruction<DATA_W>& instr, const std::string& name) {
    sc_trace(tf, instr.opcode,  name + ".opcode");
    sc_trace(tf, instr.src_a,   name + ".src_a");
    sc_trace(tf, instr.src_b,   name + ".src_b");
    sc_trace(tf, instr.dst,     name + ".dst");
    sc_trace(tf, instr.reg_a,   name + ".reg_a");
    sc_trace(tf, instr.reg_b,   name + ".reg_b");
    sc_trace(tf, instr.reg_dst, name + ".reg_dst");
    sc_trace(tf, instr.imm,     name + ".imm");
}

// Bundle para el unico puerto de carga de programa (instr_in): permite
// escribir una instruccion en cualquier direccion de la memoria interna.
template <int DATA_W = 32>
struct PE_InstrIn {
    bool valid;
    sc_uint<8> addr;
    PE_Instruction<DATA_W> instr;

    PE_InstrIn() : valid(false), addr(0), instr() {}

    inline bool operator==(const PE_InstrIn<DATA_W>& o) const {
        return valid == o.valid && addr == o.addr && instr == o.instr;
    }
};

template <int DATA_W>
inline ostream& operator<<(ostream& os, const PE_InstrIn<DATA_W>& in) {
    os << "{valid=" << in.valid << ", addr=" << in.addr << ", instr=" << in.instr << "}";
    return os;
}

template <int DATA_W>
inline void sc_trace(sc_core::sc_trace_file* tf, const PE_InstrIn<DATA_W>& in, const std::string& name) {
    sc_trace(tf, in.valid, name + ".valid");
    sc_trace(tf, in.addr,  name + ".addr");
    sc_trace(tf, in.instr, name + ".instr");
}

// Dato vectorial: VLEN lanes independientes de sc_int<DATA_W>. Usado como
// wire unico de la malla heterogenea (el escalar es un caso degenerado del
// vector) y como tipo de dato nativo de PE_vector/PE_MAC.
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

// Instruccion vectorial. Nombre distinto de PE_Instruction/PE_InstrIn a
// proposito: son estructuralmente identicos salvo el parametro VLEN (que ni
// siquiera se usa en el cuerpo, imm sigue escalar), pero dos templates de
// clase con el mismo nombre y distinta cantidad de parametros no pueden
// coexistir en la misma unidad de traduccion.
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

#endif // PE_ISA_H
