// CGRA_Mesh.h
// Fabric estructural de PEs en grid: instancia PE_T pe[ROWS][COLS], cablea los
// puertos de malla (in_N/S/E/W, out_N/S/E/W) entre vecinas con sc_signal, y
// expone solo los puertos de borde + clk/rst/enable + load_instr(). Sin
// procesos propios (SC_METHOD/SC_THREAD): puramente estructural, sin TLM.
//
// Contrato requerido de PE_T (el que ya cumple PE_scalar): clk/rst/enable
// (sc_in<bool>), in_N/in_S/in_E/in_W y out_N/out_S/out_E/out_W
// (sc_in/sc_out<sc_int<DATA_W>>), instr_in (sc_in<InstrIn> con InstrIn
// anidado como PE_T::InstrIn / PE_T::Instr). DATA_W se pasa aparte porque
// hace falta para tipar los puertos de borde de la malla; debe coincidir con
// el DATA_W usado al instanciar PE_T.

#ifndef CGRA_MESH_H
#define CGRA_MESH_H

#include <systemc.h>
#include <string>

template <typename PE_T, int ROWS, int COLS, int DATA_W = 32>
class CGRA_Mesh : public sc_core::sc_module {
public:
    typedef typename PE_T::Instr   Instr;
    typedef typename PE_T::InstrIn InstrIn;

    // ---- Control, compartido por toda la malla -----------------------------
    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    // ---- Bordes de la malla -------------------------------------------------
    sc_vector<sc_in<sc_int<DATA_W>>>  in_N,  in_S;   // tamano COLS
    sc_vector<sc_out<sc_int<DATA_W>>> out_N, out_S;  // tamano COLS
    sc_vector<sc_in<sc_int<DATA_W>>>  in_W,  in_E;   // tamano ROWS
    sc_vector<sc_out<sc_int<DATA_W>>> out_W, out_E;  // tamano ROWS

    // ---- PEs ------------------------------------------------------------
    sc_vector<PE_T> pe;  // tamano ROWS*COLS, indice r*COLS+c

    explicit CGRA_Mesh(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), out_N("out_N"), out_S("out_S"),
          in_W("in_W"), in_E("in_E"), out_W("out_W"), out_E("out_E"),
          pe("pe")
    {
        in_N.init(COLS);  out_N.init(COLS);
        in_S.init(COLS);  out_S.init(COLS);
        in_W.init(ROWS);  out_W.init(ROWS);
        in_E.init(ROWS);  out_E.init(ROWS);

        instr_sig.init(ROWS * COLS);
        // enlaces internos: dos senales por enlace (una por sentido de dato),
        // ya que cada PE tiene puertos in/out separados, no un bus compartido.
        sig_horiz_e.init(ROWS * (COLS > 1 ? COLS - 1 : 0));  // out_E -> in_W (dato hacia el este)
        sig_horiz_w.init(ROWS * (COLS > 1 ? COLS - 1 : 0));  // out_W -> in_E (dato hacia el oeste)
        sig_vert_s.init((ROWS > 1 ? ROWS - 1 : 0) * COLS);   // out_S -> in_N (dato hacia el sur)
        sig_vert_n.init((ROWS > 1 ? ROWS - 1 : 0) * COLS);   // out_N -> in_S (dato hacia el norte)

        // Nombre de cada PE como "pe_<row>_<col>" (no el indice plano que
        // sc_vector genera por defecto) para que se identifique de un
        // vistazo en el waveform.
        pe.init(ROWS * COLS, [](const char*, size_t idx) {
            int r = idx / COLS;
            int c = idx % COLS;
            std::string name = "pe_" + std::to_string(r) + "_" + std::to_string(c);
            return new PE_T(name.c_str());
        });

        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                int idx = r * COLS + c;
                PE_T& cell = pe[idx];

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

    // -----------------------------------------------------------------------
    // Programa una instruccion en la PE (row,col). Poke directo a la senal
    // interna de instr_in de esa celda (no hay puerto de instrucciones a
    // nivel de malla) — hace falta un flanco de clk para que se latchee.
    // -----------------------------------------------------------------------
    void load_instr(int row, int col, sc_dt::sc_uint<8> addr, const Instr& instr) {
        InstrIn in;
        in.valid = true;
        in.addr = addr;
        in.instr = instr;
        instr_sig[row * COLS + col].write(in);
    }

    // Deja de escribir instrucciones en (row,col) (valid=false).
    void clear_instr(int row, int col) {
        instr_sig[row * COLS + col].write(InstrIn());
    }

    void trace(sc_core::sc_trace_file* tf) const {
        for (int i = 0; i < ROWS * COLS; i++) pe[i].trace(tf);
    }

private:
    sc_vector<sc_signal<InstrIn>> instr_sig;

    sc_vector<sc_signal<sc_int<DATA_W>>> sig_horiz_e, sig_horiz_w;
    sc_vector<sc_signal<sc_int<DATA_W>>> sig_vert_s, sig_vert_n;
};

#endif // CGRA_MESH_H
