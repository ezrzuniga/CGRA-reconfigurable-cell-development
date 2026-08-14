// SumReduce8_NoC_HLS_Top_C.cpp
// Definicion de SumReduce8_NoC_HLS_Top_C (ver el .h). Programa espacial y
// secuencia de ciclos trasladados directamente de
// CGRA_Final_SumReduce8_C__TB.cpp::build_sum_program/setup_relays/
// load_sum_program/arm_case/run_case -- misma malla (equivalencia
// ciclo-a-ciclo NoC <-> directa, ver NoC_Mesh_Static_C.h), mismo programa,
// mismos margenes de ciclos.

#include "SumReduce8_NoC_HLS_Top_C.h"

typedef CGRA_Final_NoC_Mesh_C Mesh;
typedef CGRA_Final_NoC_Link   Link;
typedef CGRA_Final_NoC_Instr  Instr;

namespace sum_reduce8_noc_hls_top_detail {

static const int ROWS     = CGRA_FINAL_ROWS;
static const int COLS     = CGRA_FINAL_COLS;
static const int PROG_LEN = CGRA_FINAL_INSTR_MEM_SIZE;  // 16

struct Coord { int row, col; };
static const Coord P00 = {1, 1};
static const Coord P01 = {1, 2};
static const Coord P10 = {2, 1};
static const Coord P11 = {2, 2};

Link lane0(int32_t v) { Link l; l[0] = v; return l; }

Instr mov_instr(ap_uint<3> src, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
Instr mov_to_reg(ap_uint<3> src, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
Instr mov_reg_instr(ap_uint<5> reg_a, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
Instr accum_reg_and_dir(ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
Instr add_reg_and_reg(ap_uint<5> reg_a, ap_uint<5> reg_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = SRC_REG; i.reg_b = reg_b;
    i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
Instr add_dir_and_dir_to_reg(ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = src_a; i.src_b = src_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
Instr add_reg_and_dir_to_dir(ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<3> dst) {
    Instr i; i.opcode = OP_ADD; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b; i.dst = dst; return i;
}
Instr nop_instr() { return Instr(); }

// Arbol de reduccion, 16 direcciones, sin patron repetitivo -- identico a
// CGRA_Final_SumReduce8_C__TB.cpp::build_sum_program.
void build_sum_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN], Instr p11[PROG_LEN]) {
#pragma HLS INLINE
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); p11[a] = nop_instr(); }

    p00[1] = mov_to_reg(SRC_WEST, 0);            // reg0 = v0
    p10[1] = mov_to_reg(SRC_WEST, 0);            // reg0 = v2
    p01[1] = mov_to_reg(SRC_NORTH, 0);           // reg0 = v6

    p00[2] = mov_to_reg(SRC_NORTH, 1);           // reg1 = v4

    p00[4] = accum_reg_and_dir(0, SRC_WEST, 0);  // reg0 = v0+v1
    p10[4] = accum_reg_and_dir(0, SRC_WEST, 0);  // reg0 = v2+v3
    p01[4] = accum_reg_and_dir(0, SRC_NORTH, 0); // reg0 = v6+v7

    p00[5] = accum_reg_and_dir(1, SRC_NORTH, 1); // reg1 = v4+v5

    p00[6] = add_reg_and_reg(0, 1, 2);           // reg2 = branchA = (v0+v1)+(v4+v5)
    p10[6] = mov_reg_instr(0, DST_EAST);         // v2+v3 -> P11 (oeste de P11)
    p01[6] = mov_reg_instr(0, DST_SOUTH);        // v6+v7 -> P11 (norte de P11)

    p11[7] = add_dir_and_dir_to_reg(SRC_WEST, SRC_NORTH, 0);  // reg0 = branchB

    p11[8] = mov_reg_instr(0, DST_NORTH);        // branchB -> P01

    p01[9] = mov_instr(SRC_SOUTH, DST_WEST);     // branchB -> P00

    p00[10] = add_reg_and_dir_to_dir(2, SRC_EAST, DST_WEST);  // TOTAL = branchA+branchB -> Routing ctx1
}

Instr routing_relay_in() {
    return make_routing_config_instr_c<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
Instr routing_relay_out() {
    return make_routing_config_instr_c<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

void load_sum_program(Mesh& mesh, const Instr p00[PROG_LEN], const Instr p01[PROG_LEN],
                       const Instr p10[PROG_LEN], const Instr p11[PROG_LEN]) {
    for (int addr = 0; addr < PROG_LEN; addr++) {
        noc_mesh_program(mesh, P00.row, P00.col, addr, p00[addr]);
        noc_mesh_program(mesh, P01.row, P01.col, addr, p01[addr]);
        noc_mesh_program(mesh, P10.row, P10.col, addr, p10[addr]);
        noc_mesh_program(mesh, P11.row, P11.col, addr, p11[addr]);
    }
}

void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        noc_mesh_program(mesh, 0, 1, addr, relay);  // Vectorial: v4/v5 -> P00
        noc_mesh_program(mesh, 0, 2, addr, relay);  // Escalar:   v6/v7 -> P01
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

} // namespace sum_reduce8_noc_hls_top_detail

void SumReduce8_NoC_HLS_Top_C(const int32_t v[8], int32_t *total)
{
    using namespace sum_reduce8_noc_hls_top_detail;

    static Mesh mesh;
    static bool programmed = false;

    Link in_N[COLS], in_S[COLS], in_W[ROWS], in_E[ROWS];
    Link out_N[COLS], out_S[COLS], out_W[ROWS], out_E[ROWS];

    if (!programmed) {
        Instr p00[PROG_LEN], p01[PROG_LEN], p10[PROG_LEN], p11[PROG_LEN];
        build_sum_program(p00, p01, p10, p11);
        setup_relays(mesh);
        load_sum_program(mesh, p00, p01, p10, p11);
        programmed = true;
    }

    // arm_case: realinear pc + recargar Routing(1,0)/(2,0) (rst borra config_bank).
    step_n(mesh, 1, /*rst=*/true, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
    noc_mesh_program(mesh, P00.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(1,0): v0/v1 -> P00
    noc_mesh_program(mesh, P10.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(2,0): v2/v3 -> P10

    // t1: primer valor de cada borde.
    in_W[P00.row] = lane0(v[0]);
    in_W[P10.row] = lane0(v[2]);
    in_N[1]       = lane0(v[4]);
    in_N[2]       = lane0(v[6]);
    step_n(mesh, 3, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr0..2

    // t2: segundo valor de cada borde.
    in_W[P00.row] = lane0(v[1]);
    in_W[P10.row] = lane0(v[3]);
    in_N[1]       = lane0(v[5]);
    in_N[2]       = lane0(v[7]);
    step_n(mesh, 4, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr3..6
    step_n(mesh, 3, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr7..9
    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr10

    // Conmutar Routing(1,0) a ctx1 para sacar el TOTAL al borde real.
    noc_mesh_program(mesh, P00.row, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

    *total = out_W[P00.row][0].to_int();
}
