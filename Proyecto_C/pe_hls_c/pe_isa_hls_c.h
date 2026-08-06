// pe_isa_hls_c.h
// ISA compartida por la variante C/C++ pura del PE MAC, transliterada de
// pe/pe_isa.h: mismos opcodes/campos/anchos de bit, pero sobre ap_int/ap_uint
// (tipo nativo de Vitis HLS, sin dependencia de libSystemC) en vez de
// sc_int/sc_uint, y sin los overloads de operator<</sc_trace (no aplican
// fuera de un modulo SystemC).

#ifndef PE_ISA_HLS_C_H
#define PE_ISA_HLS_C_H

#include <ap_int.h>
#include <cstdint>

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
    OP_MUL  = 12,
    OP_MAC  = 13,
    // Punto flotante IEEE-754 de 32 bits: los mismos lanes ap_int<32> del
    // wire llevan el patron de bits de un float, reinterpretado (no
    // convertido) por float_bits_to_f32/f32_to_bits abajo. Solo tiene
    // sentido con DATA_W=32 (las 3 variantes de PE vectorial/MAC lo son por
    // defecto); en otros DATA_W estas opciones devuelven 0.
    OP_FADD = 14,
    OP_FSUB = 15,
    OP_FMUL = 16,
    OP_FMAC = 17,
    // Bucle HW de la celda escalar (ver PE_Scalar_HLS_C.h): OP_LOOP marca el
    // inicio de una region que se repite `imm` veces con salto de PC a
    // costo cero (sin recargar instr_mem), OP_ENDLOOP marca su cierre.
    // OP_PSET actualiza el registro de predicado de 1 bit que
    // pred_gate en PE_Instruction usa para condicionar el writeback de
    // cualquier otra instruccion.
    OP_LOOP    = 18,
    OP_ENDLOOP = 19,
    OP_PSET    = 20
};

enum PE_Src {
    SRC_REG   = 0,
    SRC_NORTH = 1,
    SRC_SOUTH = 2,
    SRC_EAST  = 3,
    SRC_WEST  = 4,
    SRC_IMM   = 5,
    SRC_ACC   = 6
};

enum PE_Dst {
    DST_REG   = 0,
    DST_NORTH = 1,
    DST_SOUTH = 2,
    DST_EAST  = 3,
    DST_WEST  = 4,
    DST_ALL   = 5,
    DST_ACC   = 6
};

template <int DATA_W = 32>
struct PE_Instruction {
    // 5 bits (0..31): OP_MAC=13 seguido de los 4 opcodes de punto flotante
    // (14..17) ya no caben en los 4 bits originales (max 15).
    ap_uint<5> opcode;
    ap_uint<3> src_a;
    ap_uint<3> src_b;
    ap_uint<3> dst;
    ap_uint<5> reg_a;
    ap_uint<5> reg_b;
    ap_uint<5> reg_dst;
    ap_int<DATA_W> imm;
    // Predicacion (solo consumida por la celda escalar, ver
    // PE_Scalar_HLS_C.h): si esta en 1, el writeback de esta instruccion
    // solo ocurre cuando el registro de predicado de 1 bit de la celda esta
    // en true. 0 = comportamiento de siempre (writeback incondicional).
    ap_uint<1> pred_gate;

    PE_Instruction()
        : opcode(OP_NOP), src_a(SRC_REG), src_b(SRC_REG), dst(DST_REG),
          reg_a(0), reg_b(0), reg_dst(0), imm(0), pred_gate(0) {}
};

template <int DATA_W = 32>
struct PE_InstrIn {
    bool valid;
    ap_uint<8> addr;
    PE_Instruction<DATA_W> instr;

    PE_InstrIn() : valid(false), addr(0), instr() {}
};

// Dato vectorial: VLEN lanes independientes de ap_int<DATA_W>. Wire unico de
// la malla. Default subido a 8 (de 4) para que una celda vectorial/MAC
// instanciada con el default alcance el ancho de banco de registros
// reclamado en el diagrama de Avance 1 (8 registros x 256b = VLEN(8) x
// DATA_W(32)); GEMM 2x2 sigue fijando VLEN=1 explicitamente (no usa este
// default) porque su datapath es escalar por diseno.
template <int DATA_W = 32, int VLEN = 8>
struct PE_VectorData {
    ap_int<DATA_W> lane[VLEN];

    PE_VectorData() {
        for (int i = 0; i < VLEN; i++) lane[i] = 0;
    }

    ap_int<DATA_W>& operator[](int idx) { return lane[idx]; }
    const ap_int<DATA_W>& operator[](int idx) const { return lane[idx]; }
};

// ---------------------------------------------------------------------------
// Punto flotante IEEE-754 de 32 bits sobre el mismo wire entero.
//
// El wire de la malla (Link = PE_VectorData<DATA_W,VLEN>) es homogeneo por
// diseno (ver seccion 2.5 de project.md: "wire unificado"): todas las
// celdas, sin importar su comportamiento interno, hablan el mismo tipo de
// dato en sus 4 puertos de borde. Darle a la celda vectorial un tipo de
// dato *distinto* (float en vez de ap_int) en el wire habria roto ese
// contrato para toda la malla (Routing/Memoria tendrian que aprender a
// copiar floats tambien). En vez de eso, los opcodes OP_F* reinterpretan el
// patron de bits de un lane ap_int<32> como IEEE-754 (bit_cast, no
// conversion numerica) solo del lado del ALU que los ejecuta -- el wire en
// si nunca deja de ser entero. Esto es lo mismo que hacen la mayoria de
// ISAs reales (RISC-V incluido) al compartir un banco de registros entre
// enteros y flotantes de igual ancho.
inline float f32_from_bits(int32_t bits) {
    union { int32_t i; float f; } u;
    u.i = bits;
    return u.f;
}

inline int32_t f32_to_bits(float f) {
    union { int32_t i; float f; } u;
    u.f = f;
    return u.i;
}

#endif // PE_ISA_HLS_C_H
