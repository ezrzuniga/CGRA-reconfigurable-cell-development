// GEMM_NoC_HLS_Top_C.cpp
// Definicion de GEMM_NoC_HLS_Top_C (ver el .h). El programa espacial, los
// constructores de instruccion y la secuencia de ciclos son un traslado
// directo de CGRA_Final_NoC_GEMM_C__TB.cpp::build_gemm_program/setup_b_relays/
// load_mac_program/arm_case/run_case -- misma malla, mismo programa, mismos
// margenes de ciclos, solo reempaquetados como cuerpo de una funcion top en
// vez de un main() de testbench (sin std::string/printf, nada de eso es
// sintetizable).

#include "GEMM_NoC_HLS_Top_C.h"

typedef CGRA_Final_NoC_Mesh_C Mesh;
typedef CGRA_Final_NoC_Link   Link;
typedef CGRA_Final_NoC_Instr  Instr;

namespace gemm_noc_hls_top_detail {

static const int ROWS       = CGRA_FINAL_ROWS;
static const int COLS       = CGRA_FINAL_COLS;
static const int PROG_LEN   = CGRA_FINAL_INSTR_MEM_SIZE;  // 16
static const int PROG_SLOTS = 10;

struct Coord { int row, col; };
static const Coord MAC_CELL[2][2] = {
    {{1, 1}, {1, 2}},
    {{2, 1}, {2, 2}}
};

Link lane0(int32_t v) { Link l; l[0] = v; return l; }

Instr mov_instr(ap_uint<3> src, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
Instr mac_instr(ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MAC; i.src_a = src_a; i.src_b = src_b; i.dst = dst; return i;
}
Instr nop_instr() { return Instr(); }

// Programa espacial del bloque MAC sistolico 2x2 -- identico a
// CGRA_Final_NoC_GEMM_C__TB.cpp::build_gemm_program.
void build_gemm_program(Instr prog[2][2][PROG_SLOTS]) {
#pragma HLS INLINE
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int s = 0; s < PROG_SLOTS; s++)
                prog[i][j][s] = nop_instr();

    // --- k = 0 ---------------------------------------------------------
    prog[0][0][1] = mov_instr(SRC_WEST, DST_EAST);                    // A00 -> P01
    prog[1][0][1] = mov_instr(SRC_WEST, DST_EAST);                    // A10 -> P11
    prog[0][0][2] = mov_instr(SRC_NORTH, DST_SOUTH);                  // B00 -> P10
    prog[0][1][2] = mov_instr(SRC_NORTH, DST_SOUTH);                  // B01 -> P11
    prog[0][0][3] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][1][3] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][0][3] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][1][3] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);

    // --- k = 1 (mismo patron, corrido 4 slots) --------------------------
    prog[0][0][5] = mov_instr(SRC_WEST, DST_EAST);
    prog[1][0][5] = mov_instr(SRC_WEST, DST_EAST);
    prog[0][0][6] = mov_instr(SRC_NORTH, DST_SOUTH);
    prog[0][1][6] = mov_instr(SRC_NORTH, DST_SOUTH);
    prog[0][0][7] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][1][7] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][0][7] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][1][7] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);

    // --- lectura de acumuladores -----------------------------------------
    prog[0][0][8] = mov_instr(SRC_ACC, DST_WEST);   // -> Routing(1,0) ctx1 -> out_W[1]
    prog[0][1][8] = mov_instr(SRC_ACC, DST_EAST);   // -> out_E[1]
    prog[1][0][8] = mov_instr(SRC_ACC, DST_SOUTH);  // -> out_S[1]
    prog[1][1][8] = mov_instr(SRC_ACC, DST_EAST);   // -> out_E[2]
}

Instr routing_relay_in() {
    return make_routing_config_instr_c<CGRA_FINAL_DATA_W>(RC_NONE, RC_FROM_W, RC_FROM_W, RC_NONE);
}
Instr routing_relay_out() {
    return make_routing_config_instr_c<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

void setup_b_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        noc_mesh_program(mesh, 0, 1, addr, relay);  // Vectorial
        noc_mesh_program(mesh, 0, 2, addr, relay);  // Escalar
    }
}

void load_mac_program(Mesh& mesh, const Instr prog[2][2][PROG_SLOTS]) {
    for (int addr = 0; addr < PROG_LEN; addr++)
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                noc_mesh_program(mesh, MAC_CELL[i][j].row, MAC_CELL[i][j].col, addr,
                                 addr < PROG_SLOTS ? prog[i][j][addr] : nop_instr());
}

void step_n(Mesh& mesh, int n, bool rst,
            Link in_N[COLS], Link in_S[COLS], Link in_W[ROWS], Link in_E[ROWS],
            Link out_N[COLS], Link out_S[COLS], Link out_W[ROWS], Link out_E[ROWS]) {
step_n_loop:
    for (int i = 0; i < n; i++)
        cgra_final_noc_step(mesh, rst, /*enable=*/true, in_N, in_S, in_W, in_E,
                            out_N, out_S, out_W, out_E);
}

} // namespace gemm_noc_hls_top_detail

void GEMM_NoC_HLS_Top_C(const int32_t A[2][2], const int32_t B[2][2], int32_t C[2][2])
{
    using namespace gemm_noc_hls_top_detail;

    static Mesh mesh; // unico estado con memoria del diseno -- persiste entre llamadas
    static bool programmed = false;

    Link in_N[COLS], in_S[COLS], in_W[ROWS], in_E[ROWS];
    Link out_N[COLS], out_S[COLS], out_W[ROWS], out_E[ROWS];

    if (!programmed) {
        Instr prog[2][2][PROG_SLOTS];
        build_gemm_program(prog);
        setup_b_relays(mesh);
        load_mac_program(mesh, prog);
        programmed = true;
    }

    // arm_case: limpiar acumuladores, alinear pc (pulso de rst) y recargar
    // el contexto de entrada de Routing(1,0)/(2,0) (rst borra config_bank).
    noc_mesh_clear_acc(mesh);
    step_n(mesh, 1, /*rst=*/true, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
    noc_mesh_program(mesh, 1, 0, /*ctx=*/0, routing_relay_in());
    noc_mesh_program(mesh, 2, 0, /*ctx=*/0, routing_relay_in());

    // k = 0: primera columna de A, primera fila de B.
    in_W[1] = lane0(A[0][0]); in_W[2] = lane0(A[1][0]);
    in_N[1] = lane0(B[0][0]); in_N[2] = lane0(B[0][1]);
    step_n(mesh, 4, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

    // k = 1: segunda columna de A, segunda fila de B.
    in_W[1] = lane0(A[0][1]); in_W[2] = lane0(A[1][1]);
    in_N[1] = lane0(B[1][0]); in_N[2] = lane0(B[1][1]);
    step_n(mesh, 4, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);

    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr8

    // Conmutar Routing(1,0) a ctx1 (enlace interno -> borde real oeste) para
    // sacar C[0][0].
    noc_mesh_program(mesh, 1, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr9

    C[0][0] = out_W[1][0].to_int();   // via Routing(1,0) ctx1
    C[0][1] = out_E[1][0].to_int();   // borde real directo de P01
    C[1][0] = out_S[1][0].to_int();   // borde real directo de P10
    C[1][1] = out_E[2][0].to_int();   // borde real directo de P11
}
