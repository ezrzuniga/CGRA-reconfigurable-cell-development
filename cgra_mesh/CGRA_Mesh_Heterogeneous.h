// CGRA_Mesh_Heterogeneous.h
// Hermana heterogenea de CGRA_Mesh.h: mismo wiring estructural (in_N/S/E/W,
// out_N/S/E/W entre vecinas con sc_signal, sin procesos propios), pero cada celda
// puede ser PE_Scalar_Cell o PE_Vector_Cell segun un layout dado en la construccion.
// El wire comun de la malla es PE_Base::Link (PE_VectorData<DATA_W,VLEN>); las
// celdas escalares hacen de puente (broadcast/lane 0) puertas adentro de
// PE_Scalar_Cell, esta malla no sabe nada de esa conversion.
//
// CGRA_Mesh.h queda intacta a proposito (ver CLAUDE.md): esta es una malla nueva,
// no un reemplazo.

#ifndef CGRA_MESH_HETEROGENEOUS_H
#define CGRA_MESH_HETEROGENEOUS_H

#include <systemc.h>
#include <string>
#include <vector>
#include "PE_Base.h"
#include "PE_Scalar_Cell.h"
#include "PE_Vector_Cell.h"
#include "PE_MAC_Cell.h"

template <int ROWS, int COLS, int DATA_W = 32, int VLEN = 4,
          int NUM_REGS = 8, int INSTR_MEM_SIZE = 16>
class CGRA_Mesh_Heterogeneous : public sc_core::sc_module {
public:
    typedef PE_Base<DATA_W, VLEN> PE;
    typedef typename PE::Instr   Instr;
    typedef typename PE::InstrIn InstrIn;
    typedef typename PE::Link    Link;

    // ---- Control, compartido por toda la malla -----------------------------
    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    // ---- Bordes de la malla -------------------------------------------------
    sc_vector<sc_in<Link>>  in_N,  in_S;   // tamano COLS
    sc_vector<sc_out<Link>> out_N, out_S;  // tamano COLS
    sc_vector<sc_in<Link>>  in_W,  in_E;   // tamano ROWS
    sc_vector<sc_out<Link>> out_W, out_E;  // tamano ROWS

    // ---- PEs ------------------------------------------------------------
    sc_vector<PE> pe;  // tamano ROWS*COLS, indice r*COLS+c, tipo por celda via layout

    CGRA_Mesh_Heterogeneous(sc_core::sc_module_name name, const std::vector<CellKind>& layout)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), out_N("out_N"), out_S("out_S"),
          in_W("in_W"), in_E("in_E"), out_W("out_W"), out_E("out_E"),
          pe("pe")
    {
        if ((int)layout.size() != ROWS * COLS) {
            SC_REPORT_FATAL("CGRA_Mesh_Heterogeneous", "layout.size() != ROWS*COLS");
        }

        in_N.init(COLS);  out_N.init(COLS);
        in_S.init(COLS);  out_S.init(COLS);
        in_W.init(ROWS);  out_W.init(ROWS);
        in_E.init(ROWS);  out_E.init(ROWS);

        instr_sig.init(ROWS * COLS);
        sig_horiz_e.init(ROWS * (COLS > 1 ? COLS - 1 : 0));
        sig_horiz_w.init(ROWS * (COLS > 1 ? COLS - 1 : 0));
        sig_vert_s.init((ROWS > 1 ? ROWS - 1 : 0) * COLS);
        sig_vert_n.init((ROWS > 1 ? ROWS - 1 : 0) * COLS);

        // Nombre de cada PE como "pe_<row>_<col>" y tipo concreto segun layout[idx].
        pe.init(ROWS * COLS, [&layout](const char*, size_t idx) -> PE* {
            int r = (int)idx / COLS;
            int c = (int)idx % COLS;
            std::string name = "pe_" + std::to_string(r) + "_" + std::to_string(c);
            if (layout[idx] == CellKind::VECTOR) {
                return new PE_Vector_Cell<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>(name.c_str());
            }
            if (layout[idx] == CellKind::MAC) {
                return new PE_MAC_Cell<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>(name.c_str());
            }
            return new PE_Scalar_Cell<DATA_W, VLEN, NUM_REGS, INSTR_MEM_SIZE>(name.c_str());
        });

        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                int idx = r * COLS + c;
                PE& cell = pe[idx];

                cell.clk(clk);
                cell.rst(rst);
                cell.enable(enable);
                cell.instr_in(instr_sig[idx]);

                if (r == 0) {
                    cell.in_N(in_N[c]);
                    cell.out_N(out_N[c]);
                } else {
                    cell.in_N(sig_vert_s[(r - 1) * COLS + c]);
                    cell.out_N(sig_vert_n[(r - 1) * COLS + c]);
                }

                if (r == ROWS - 1) {
                    cell.in_S(in_S[c]);
                    cell.out_S(out_S[c]);
                } else {
                    cell.in_S(sig_vert_n[r * COLS + c]);
                    cell.out_S(sig_vert_s[r * COLS + c]);
                }

                if (c == 0) {
                    cell.in_W(in_W[r]);
                    cell.out_W(out_W[r]);
                } else {
                    cell.in_W(sig_horiz_e[r * (COLS - 1) + (c - 1)]);
                    cell.out_W(sig_horiz_w[r * (COLS - 1) + (c - 1)]);
                }

                if (c == COLS - 1) {
                    cell.in_E(in_E[r]);
                    cell.out_E(out_E[r]);
                } else {
                    cell.in_E(sig_horiz_w[r * (COLS - 1) + c]);
                    cell.out_E(sig_horiz_e[r * (COLS - 1) + c]);
                }
            }
        }
    }

    void load_instr(int row, int col, sc_dt::sc_uint<8> addr, const Instr& instr) {
        InstrIn in;
        in.valid = true;
        in.addr = addr;
        in.instr = instr;
        instr_sig[row * COLS + col].write(in);
    }

    void clear_instr(int row, int col) {
        instr_sig[row * COLS + col].write(InstrIn());
    }

    void trace(sc_core::sc_trace_file* tf) const {
        for (int i = 0; i < ROWS * COLS; i++) pe[i].trace(tf);
    }

private:
    sc_vector<sc_signal<InstrIn>> instr_sig;

    sc_vector<sc_signal<Link>> sig_horiz_e, sig_horiz_w;
    sc_vector<sc_signal<Link>> sig_vert_s, sig_vert_n;
};

#endif // CGRA_MESH_HETEROGENEOUS_H
