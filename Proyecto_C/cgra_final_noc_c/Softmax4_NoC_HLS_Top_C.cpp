// Softmax4_NoC_HLS_Top_C.cpp
// Definicion de Softmax4_NoC_HLS_Top_C (ver el .h). Programa espacial y
// secuencia de ciclos trasladados directamente de
// CGRA_Final_Softmax4_C__TB.cpp::build_softmax_program/setup_relays/
// arm_case/run_case.

#include "Softmax4_NoC_HLS_Top_C.h"

typedef CGRA_Final_NoC_Mesh_C Mesh;
typedef CGRA_Final_NoC_Link   Link;
typedef CGRA_Final_NoC_Instr  Instr;

namespace softmax4_noc_hls_top_detail {

static const int ROWS     = CGRA_FINAL_ROWS;
static const int COLS     = CGRA_FINAL_COLS;
static const int PROG_LEN = CGRA_FINAL_INSTR_MEM_SIZE;  // 16
static const int32_t EXP2_SHIFT_BIAS = 9;  // EXP2(x) = 1 << (x + 9), valido para x en [-9, 9]

struct Coord { int row, col; };
static const Coord P00 = {1, 1};
static const Coord P01 = {1, 2};
static const Coord P10 = {2, 1};

Link lane0(int32_t v) { Link l; l[0] = v; return l; }

Instr mov_instr(ap_uint<3> src, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
Instr mov_reg_instr(ap_uint<5> reg_a, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
Instr add_imm_to_reg(ap_uint<3> src, int32_t imm, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = src; i.src_b = SRC_IMM; i.imm = imm;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
// reg[reg_dst] = 1 << reg[shamt_reg]  (EXP2 exacto via shift)
Instr shl_one_by_reg(ap_uint<5> shamt_reg, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_SLL; i.src_a = SRC_IMM; i.imm = 1; i.src_b = SRC_REG; i.reg_b = shamt_reg;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
Instr accum_reg_and_reg(ap_uint<5> reg_a, ap_uint<5> reg_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = SRC_REG; i.reg_b = reg_b;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
Instr accum_reg_and_dir(ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
Instr nop_instr() { return Instr(); }

// Registros de P00: 0=tmp0/e0, 1=tmp1/e1, 2=SUM -- identico a
// CGRA_Final_Softmax4_C__TB.cpp::build_softmax_program.
void build_softmax_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN]) {
#pragma HLS INLINE
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); }

    p00[1] = add_imm_to_reg(SRC_WEST,  EXP2_SHIFT_BIAS, 0);   // tmp0 = x0+9
    p10[1] = add_imm_to_reg(SRC_WEST,  EXP2_SHIFT_BIAS, 0);   // tmp2 = x2+9
    p01[1] = add_imm_to_reg(SRC_NORTH, EXP2_SHIFT_BIAS, 0);   // tmp3 = x3+9

    p00[2] = add_imm_to_reg(SRC_NORTH, EXP2_SHIFT_BIAS, 1);   // tmp1 = x1+9
    p10[2] = shl_one_by_reg(0, 0);                            // e2 = 1<<tmp2
    p01[2] = shl_one_by_reg(0, 0);                            // e3 = 1<<tmp3

    p00[3] = shl_one_by_reg(0, 0);                            // e0 = 1<<tmp0
    p00[4] = shl_one_by_reg(1, 1);                            // e1 = 1<<tmp1

    p10[4] = mov_reg_instr(0, DST_NORTH);                     // e2 -> P00 (sur de P00)
    p01[4] = mov_reg_instr(0, DST_WEST);                      // e3 -> P00 (este de P00)

    p00[5] = accum_reg_and_reg(0, 1, 2);                      // reg2 = e0+e1
    p00[6] = accum_reg_and_dir(2, SRC_SOUTH, 2);              // reg2 += e2
    p00[7] = accum_reg_and_dir(2, SRC_EAST, 2);               // reg2 += e3 = SUM

    p00[8] = mov_reg_instr(2, DST_WEST);                      // SUM -> Routing ctx1 -> out_W[1]
    p00[9] = mov_reg_instr(0, DST_WEST);                      // e0  -> mismo puerto
}

Instr routing_relay_in() {
    return make_routing_config_instr_c<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
Instr routing_relay_out() {
    return make_routing_config_instr_c<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

void load_softmax_program(Mesh& mesh, const Instr p00[PROG_LEN],
                           const Instr p01[PROG_LEN], const Instr p10[PROG_LEN]) {
    for (int addr = 0; addr < PROG_LEN; addr++) {
        noc_mesh_program(mesh, P00.row, P00.col, addr, p00[addr]);
        noc_mesh_program(mesh, P01.row, P01.col, addr, p01[addr]);
        noc_mesh_program(mesh, P10.row, P10.col, addr, p10[addr]);
    }
}

void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        noc_mesh_program(mesh, 0, 1, addr, relay);  // Vectorial: x1 -> P00
        noc_mesh_program(mesh, 0, 2, addr, relay);  // Escalar:   x3 -> P01
    }
}

void step_n(Mesh& mesh, int n, bool rst,
            Link in_N[COLS], Link in_S[COLS], Link in_W[ROWS], Link in_E[ROWS],
            Link out_N[COLS], Link out_S[COLS], Link out_W[ROWS], Link out_E[ROWS]) {
step_n_loop:
    for (int i = 0; i < n; i++)
        cgra_final_noc_step(mesh, rst, /*enable=*/true, in_N, in_S, in_W, in_E,
                            out_N, out_S, out_W, out_E);
}

} // namespace softmax4_noc_hls_top_detail

void Softmax4_NoC_HLS_Top_C(int32_t x0, int32_t x1, int32_t x2, int32_t x3,
                             int32_t *sum_out, int32_t *e0_out)
{
    using namespace softmax4_noc_hls_top_detail;

    static Mesh mesh;
    static bool programmed = false;

    Link in_N[COLS], in_S[COLS], in_W[ROWS], in_E[ROWS];
    Link out_N[COLS], out_S[COLS], out_W[ROWS], out_E[ROWS];

    if (!programmed) {
        Instr p00[PROG_LEN], p01[PROG_LEN], p10[PROG_LEN];
        build_softmax_program(p00, p01, p10);
        setup_relays(mesh);
        load_softmax_program(mesh, p00, p01, p10);
        programmed = true;
    }

    // arm_case: realinear pc + recargar Routing(1,0)/(2,0) (rst borra config_bank).
    step_n(mesh, 1, /*rst=*/true, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
    noc_mesh_program(mesh, P00.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(1,0): x0 -> P00
    noc_mesh_program(mesh, P10.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(2,0): x2 -> P10

    in_W[P00.row] = lane0(x0);
    in_N[1]       = lane0(x1);
    in_W[P10.row] = lane0(x2);
    in_N[2]       = lane0(x3);

    step_n(mesh, 8, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr0..7
    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr8: SUM -> puerto oeste de P00

    // Conmutar Routing(1,0) a ctx1 para sacar los resultados al borde real.
    noc_mesh_program(mesh, P00.row, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr9: e0 en registro; Routing saca SUM
    *sum_out = out_W[P00.row][0].to_int();

    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr10: Routing saca e0
    *e0_out = out_W[P00.row][0].to_int();
}
