// FFT4_NoC_HLS_Top_C.cpp
// Definicion de FFT4_NoC_HLS_Top_C (ver el .h). Programa espacial, empaquetado
// de lanes y secuencia de ciclos trasladados directamente de
// CGRA_Final_FFT4_C__TB.cpp::build_fft_program/setup_relays/arm_case/run_case.

#include "FFT4_NoC_HLS_Top_C.h"

typedef CGRA_Final_NoC_Mesh_C Mesh;
typedef CGRA_Final_NoC_Link   Link;
typedef CGRA_Final_NoC_Instr  Instr;

namespace fft4_noc_hls_top_detail {

static const int ROWS     = CGRA_FINAL_ROWS;
static const int COLS     = CGRA_FINAL_COLS;
static const int PROG_LEN = CGRA_FINAL_INSTR_MEM_SIZE;  // 16

struct Coord { int row, col; };
static const Coord P00 = {1, 1};
static const Coord P01 = {1, 2};
static const Coord P10 = {2, 1};
static const Coord P11 = {2, 2};

Link link4(int32_t a, int32_t b, int32_t c, int32_t d) {
    Link v; v[0] = a; v[1] = b; v[2] = c; v[3] = d; return v;
}

Instr mov_instr(ap_uint<3> src, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
Instr mov_reg_instr(ap_uint<5> reg_a, ap_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_REG; i.reg_a = reg_a; i.dst = dst; return i;
}
Instr op_to_reg(ap_uint<4> op, ap_uint<3> src_a, ap_uint<3> src_b, ap_uint<5> reg_dst) {
    Instr i; i.opcode = op; i.src_a = src_a; i.src_b = src_b; i.dst = DST_REG; i.reg_dst = reg_dst; return i;
}
Instr op_reg_and_dir(ap_uint<4> op, ap_uint<5> reg_a, ap_uint<3> src_b, ap_uint<3> dst) {
    Instr i; i.opcode = op; i.src_a = SRC_REG; i.reg_a = reg_a; i.src_b = src_b; i.dst = dst; return i;
}
Instr op_dir_and_reg(ap_uint<4> op, ap_uint<3> src_a, ap_uint<5> reg_b, ap_uint<3> dst) {
    Instr i; i.opcode = op; i.src_a = src_a; i.src_b = SRC_REG; i.reg_b = reg_b; i.dst = dst; return i;
}
Instr nop_instr() { return Instr(); }

// Cadena secuencial de dependencias (mariposas), sin patron repetitivo --
// identico a CGRA_Final_FFT4_C__TB.cpp::build_fft_program.
void build_fft_program(Instr p00[PROG_LEN], Instr p01[PROG_LEN], Instr p10[PROG_LEN], Instr p11[PROG_LEN]) {
#pragma HLS INLINE
    for (int a = 0; a < PROG_LEN; a++) { p00[a] = nop_instr(); p01[a] = nop_instr(); p10[a] = nop_instr(); p11[a] = nop_instr(); }

    p10[1] = mov_instr(SRC_WEST, DST_NORTH);                 // x2 -> P00 (sur)
    p00[1] = mov_instr(SRC_NORTH, DST_EAST);                 // x1 -> P01 (oeste)

    p00[2] = op_to_reg(OP_ADD, SRC_WEST, SRC_SOUTH, 0);      // reg0 = a = x0+x2
    p00[3] = op_to_reg(OP_SUB, SRC_WEST, SRC_SOUTH, 1);      // reg1 = b = x0-x2
    p10[3] = mov_instr(SRC_WEST, DST_EAST);                  // x3 -> P11 (oeste)
    p11[4] = mov_instr(SRC_WEST, DST_NORTH);                 // x3 -> P01 (sur)

    p01[5] = op_to_reg(OP_ADD, SRC_WEST, SRC_SOUTH, 0);      // reg0 = c = x1+x3
    p01[6] = op_to_reg(OP_SUB, SRC_WEST, SRC_SOUTH, 1);      // reg1 = d = twiddle(x1-x3)

    p00[7] = mov_reg_instr(1, DST_EAST);                     // b -> P01
    p01[7] = mov_reg_instr(0, DST_WEST);                     // c -> P00

    p00[8] = op_reg_and_dir(OP_ADD, 0, SRC_EAST, DST_WEST);  // X0 = a+c (lanes 0-1) -> Routing ctx1
    p01[8] = op_dir_and_reg(OP_ADD, SRC_WEST, 1, DST_EAST);  // X1 = b+d (lanes 2-3) -> out_E[1]

    p00[9] = op_reg_and_dir(OP_SUB, 0, SRC_EAST, DST_WEST);  // X2 = a-c (lanes 0-1) -> Routing ctx1
    p01[9] = op_dir_and_reg(OP_SUB, SRC_WEST, 1, DST_EAST);  // X3 = b-d (lanes 2-3) -> out_E[1]
}

Instr routing_relay_in() {
    return make_routing_config_instr_c<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_NONE);
}
Instr routing_relay_out() {
    return make_routing_config_instr_c<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

void load_fft_program(Mesh& mesh, const Instr p00[PROG_LEN], const Instr p01[PROG_LEN],
                       const Instr p10[PROG_LEN], const Instr p11[PROG_LEN]) {
    for (int addr = 0; addr < PROG_LEN; addr++) {
        noc_mesh_program(mesh, P00.row, P00.col, addr, p00[addr]);
        noc_mesh_program(mesh, P01.row, P01.col, addr, p01[addr]);
        noc_mesh_program(mesh, P10.row, P10.col, addr, p10[addr]);
        noc_mesh_program(mesh, P11.row, P11.col, addr, p11[addr]);
    }
}

// Solo Vectorial participa (releva x1 hacia P00) -- Escalar queda fuera del
// camino de datos de esta FFT (destruye la parte imaginaria, ver comentario
// de cabecera de CGRA_Final_FFT4_C__TB.cpp).
void setup_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++)
        noc_mesh_program(mesh, 0, 1, addr, relay);  // Vectorial: x1 -> P00
}

void step_n(Mesh& mesh, int n, bool rst,
            Link in_N[COLS], Link in_S[COLS], Link in_W[ROWS], Link in_E[ROWS],
            Link out_N[COLS], Link out_S[COLS], Link out_W[ROWS], Link out_E[ROWS]) {
step_n_loop:
    for (int i = 0; i < n; i++)
        cgra_final_noc_step(mesh, rst, /*enable=*/true, in_N, in_S, in_W, in_E,
                            out_N, out_S, out_W, out_E);
}

} // namespace fft4_noc_hls_top_detail

void FFT4_NoC_HLS_Top_C(
    int32_t x0_re, int32_t x0_im, int32_t x1_re, int32_t x1_im,
    int32_t x2_re, int32_t x2_im, int32_t x3_re, int32_t x3_im,
    int32_t *X0_re, int32_t *X0_im, int32_t *X1_re, int32_t *X1_im,
    int32_t *X2_re, int32_t *X2_im, int32_t *X3_re, int32_t *X3_im)
{
    using namespace fft4_noc_hls_top_detail;

    static Mesh mesh;
    static bool programmed = false;

    Link in_N[COLS], in_S[COLS], in_W[ROWS], in_E[ROWS];
    Link out_N[COLS], out_S[COLS], out_W[ROWS], out_E[ROWS];

    if (!programmed) {
        Instr p00[PROG_LEN], p01[PROG_LEN], p10[PROG_LEN], p11[PROG_LEN];
        build_fft_program(p00, p01, p10, p11);
        setup_relays(mesh);
        load_fft_program(mesh, p00, p01, p10, p11);
        programmed = true;
    }

    // arm_case: realinear pc + recargar Routing(1,0)/(2,0) (rst borra config_bank).
    step_n(mesh, 1, /*rst=*/true, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
    noc_mesh_program(mesh, P00.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(1,0): x0 -> P00
    noc_mesh_program(mesh, P10.row, 0, /*ctx=*/0, routing_relay_in());  // Routing(2,0): x2/x3 -> P10

    // Empaquetado de lanes: entradas SIN twiddle (x0, x2) llevan [re,im,re,im];
    // entradas CON twiddle -j (x1, x3) llevan [re,im,im,-re] (ver comentario de
    // cabecera del .h). x3 todavia no se escribe: comparte in_W[P10.row] con x2
    // por multiplexado en el tiempo.
    in_W[P00.row] = link4(x0_re, x0_im, x0_re, x0_im);
    in_N[1]       = link4(x1_re, x1_im, x1_im, -x1_re);
    in_W[P10.row] = link4(x2_re, x2_im, x2_re, x2_im);
    step_n(mesh, 2, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr0..1

    // in_W[P10.row] = x3: P10 ya relevo x2 en addr1, nada se pisa.
    in_W[P10.row] = link4(x3_re, x3_im, x3_im, -x3_re);
    step_n(mesh, 6, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr2..7
    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr8: X0=a+c (P00), X1=b+d (P01)

    *X1_re = out_E[1][2].to_int();
    *X1_im = out_E[1][3].to_int();

    // Conmutar Routing(1,0) a ctx1 para sacar X0/X2 al borde real oeste.
    noc_mesh_program(mesh, P00.row, 0, /*ctx=*/1, routing_relay_out());
    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr9: X2/X3 en las celdas; Routing saca X0

    *X0_re = out_W[1][0].to_int();
    *X0_im = out_W[1][1].to_int();
    *X3_re = out_E[1][2].to_int();
    *X3_im = out_E[1][3].to_int();

    step_n(mesh, 1, /*rst=*/false, in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);  // addr10: Routing saca X2

    *X2_re = out_W[1][0].to_int();
    *X2_im = out_W[1][1].to_int();
}
