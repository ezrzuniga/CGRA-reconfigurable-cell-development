// GEMM_2x2_HLS_Top_C.cpp
// Definicion de GEMM_2x2_HLS_Top_C (ver GEMM_2x2_HLS_Top_C.h para el diseno
// completo). Separada del header para que Vitis HLS encuentre el top como
// una funcion real (no inline) al analizar la unidad de traduccion del
// diseno.

#include "GEMM_2x2_HLS_Top_C.h"

namespace gemm_2x2_hls_top_c_detail {

struct MeshDrive {
    bool rst;
    GemmLink_C bound_in_W[GEMM_ROWS];
    GemmLink_C bound_in_N[GEMM_COLS];
    GemmInstrIn_C load[GEMM_ROWS][GEMM_COLS];

    MeshDrive() : rst(false) {}
};

inline GemmLink_C scalar_link_c(ap_int<32> v) {
    GemmLink_C l;
    l[0] = v;
    return l;
}

inline void present_phase_c(MeshDrive& d, ap_int<32> a0, ap_int<32> a1, ap_int<32> b0, ap_int<32> b1) {
    d.bound_in_W[0] = scalar_link_c(a0);
    d.bound_in_W[1] = scalar_link_c(a1);
    d.bound_in_N[0] = scalar_link_c(b0);
    d.bound_in_N[1] = scalar_link_c(b1);
}

enum State {
    ST_CLEAR_ACC, ST_REALIGN, ST_RELOAD, ST_PHASE0, ST_PHASE1, ST_WAIT_DONE, ST_DONE
};

} // namespace gemm_2x2_hls_top_c_detail

void GEMM_2x2_HLS_Top_C(
    bool start, bool& done,
    ap_int<32> a00, ap_int<32> a01, ap_int<32> a10, ap_int<32> a11,
    ap_int<32> b00, ap_int<32> b01, ap_int<32> b10, ap_int<32> b11,
    ap_int<32>& c00, ap_int<32>& c01, ap_int<32>& c10, ap_int<32>& c11)
{
    using namespace gemm_2x2_hls_top_c_detail;

    done = false;
    if (!start) return;

    GemmInstr_C prog[GEMM_ROWS][GEMM_COLS][GEMM_INSTR_MEM_SIZE];
    gemm_program_c(prog);

    GemmMesh_C mesh; // instr_mem/reg_file/acc/out_* nacen en 0/NOP, como al elaborar el sc_module

    State state = ST_CLEAR_ACC;
    ap_uint<3> cnt = 0;
    MeshDrive curr; // rst=false, load invalido, bordes en 0 -- mismo arranque que BOOT_CLEAR/IDLE

    // in_S/in_E de borde nunca se manejan desde el host (mismo precedente que
    // in_S0_sig/in_S1_sig/in_E0_sig/in_E1_sig en GEMM_2x2_HLS_Top.h, que
    // tampoco se escriben nunca): ningun slot del programa espacial lee
    // SRC_SOUTH ni SRC_EAST, asi que quedan fijos en 0.
    GemmLink_C bound_in_S_unused[GEMM_COLS];
    GemmLink_C bound_in_E_unused[GEMM_ROWS];

gemm_cycle_loop:
    for (;;) {
        mesh_step(mesh, curr.rst, /*enable=*/true,
                  curr.bound_in_N, bound_in_S_unused,
                  curr.bound_in_W, bound_in_E_unused,
                  curr.load);

        MeshDrive nxt = curr; // "pegajoso": retiene lo del ciclo anterior salvo que este case lo cambie

        switch (state) {
            case ST_CLEAR_ACC: {
                GemmInstr_C clr = gemm_clear_acc_instr_c();
                for (int r = 0; r < GEMM_ROWS; r++)
                    for (int c = 0; c < GEMM_COLS; c++) {
                        nxt.load[r][c].valid = true;
                        nxt.load[r][c].addr = cnt;
                        nxt.load[r][c].instr = clr;
                    }
                if (cnt == 3) { state = ST_REALIGN; cnt = 0; } else cnt = cnt + 1;
                break;
            }

            case ST_REALIGN:
                nxt.rst = true;
                state = ST_RELOAD;
                break;

            case ST_RELOAD:
                nxt.rst = false;
                for (int r = 0; r < GEMM_ROWS; r++)
                    for (int c = 0; c < GEMM_COLS; c++) {
                        nxt.load[r][c].valid = true;
                        nxt.load[r][c].addr = cnt;
                        nxt.load[r][c].instr = prog[r][c][cnt.to_uint()];
                    }
                if (cnt == 3) {
                    present_phase_c(nxt, a00, a10, b00, b01);
                    state = ST_PHASE0; cnt = 0;
                } else {
                    cnt = cnt + 1;
                }
                break;

            case ST_PHASE0:
                if (cnt == 3) {
                    present_phase_c(nxt, a01, a11, b10, b11);
                    state = ST_PHASE1; cnt = 0;
                } else {
                    present_phase_c(nxt, a00, a10, b00, b01);
                    cnt = cnt + 1;
                }
                break;

            case ST_PHASE1:
                present_phase_c(nxt, a01, a11, b10, b11);
                if (cnt == 3) { state = ST_WAIT_DONE; cnt = 0; } else cnt = cnt + 1;
                break;

            case ST_WAIT_DONE:
                state = ST_DONE;
                break;

            case ST_DONE:
                c00 = mesh.pe[0][0].out_W[0];
                c01 = mesh.pe[0][1].out_E[0];
                c10 = mesh.pe[1][0].out_W[0];
                c11 = mesh.pe[1][1].out_E[0];
                done = true;
                return;
        }

        curr = nxt;
    }
}
