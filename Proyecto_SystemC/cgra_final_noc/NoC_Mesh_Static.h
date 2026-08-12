// NoC_Mesh_Static.h
// Version NoC (packet-switched) de mesh_hls/CGRA_Mesh_Static.h: mismo API
// publico (load_instr/clear_instr/cell<I>()/trace(), mismos puertos de borde
// externos in_N/in_S/in_W/in_E y out_N/out_S/in_W/out_E de tipo Link, mismo
// contrato ROWS*COLS de tipos de celda por template), pero el tejido interno
// que conecta las ROWS*COLS celdas ya no son wires punto a punto: es una
// malla de NoC_Router (uno por posicion, ver NoC_Router.h) que reenvia
// paquetes con header dest_row/dest_col en vez de mux-ear un wire fijo. Ver
// NoC_Packet.h para la comparacion completa "malla directa" vs "NoC".
//
// Reusa (sin modificar) la maquinaria de almacenamiento heterogeneo de
// mesh_hls/CGRA_Mesh_Static.h (namespace cgra_mesh_static_detail: CellChain +
// Getter, cadena de herencia recursiva en vez de std::tuple -- ver el
// comentario de cabecera de ese archivo para el porque) en vez de
// reimplementarla: el problema de "guardar ROWS*COLS tipos heterogeneos con
// sc_module_name valido" ya esta resuelto ahi y es identico aca.
//
// Cada celda de computo sigue exponiendo EXACTAMENTE el mismo contrato de 4
// puertos que ya tenia (in_N/in_S/in_E/in_W, out_N/in_S/out_E/out_W) -- no se
// toca ninguna celda existente. Lo que cambia es a que se conectan esos 4
// puertos: en vez de ir directo al vecino (o al borde externo), van al puerto
// LOCAL del router en su misma posicion (celda.out_X <-> router.in_L_X,
// router.out_L_X <-> celda.in_X, exactamente la misma convencion que
// pe_hls/routing/Routing_Cell.h ya usa para "la PE adjunta").
//
// Puente de borde (Link <-> NoC_Packet): los puertos externos de esta malla
// siguen siendo Link liso (bridge_boundary_in/out, mas abajo) para que un
// testbench que ya sabe manejar CGRA_Mesh_Static (p. ej. cgra_final/CGRA_Final_Mesh__TB.cpp)
// pueda manejar esta version sin cambiar un tipo de puerto. El header que se
// le asigna a un paquete que entra por un borde externo es siempre la
// posicion del PROPIO router de entrada (dest = su (row,col)): por
// construccion, un dato que en la malla de wires directos llegaba
// directamente al puerto in_X de la celda en esa posicion de borde debe
// seguir llegando a esa MISMA celda aca -- cualquier relevo posterior hacia
// otra celda (p. ej. Routing_Cell reenviando el operando "oeste" hacia el
// bloque MAC, ver cgra_final/CGRA_Final_Mesh.h) lo sigue haciendo esa celda
// con su propia logica existente, sin que la fabrica NoC necesite saber nada
// de esa decision.

#ifndef NOC_MESH_STATIC_H
#define NOC_MESH_STATIC_H

#include <systemc.h>
#include <string>
#include <cstddef>
#include <utility>
#include "../pe_hls/pe_isa.h"
#include "../mesh_hls/CGRA_Mesh_Static.h"  // reusa cgra_mesh_static_detail::CellChain/Getter
#include "NoC_Packet.h"
#include "NoC_Router.h"

template <int ROWS, int COLS, int DATA_W, int VLEN, typename... CellTs>
class NoC_Mesh_Static : public sc_core::sc_module {
    static_assert(sizeof...(CellTs) == ROWS * COLS,
                  "NoC_Mesh_Static: la lista de tipos de celda debe tener ROWS*COLS elementos");
public:
    typedef PE_VectorData<DATA_W, VLEN> Link;
    typedef PE_Instruction<DATA_W>      Instr;
    typedef PE_InstrIn<DATA_W>          InstrIn;
    typedef NoC_Packet<DATA_W, VLEN>    Packet;
    typedef NoC_Router<DATA_W, VLEN>    Router;

    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;

    sc_vector<sc_in<Link>>  in_N,  in_S;   // tamano COLS
    sc_vector<sc_out<Link>> out_N, out_S;  // tamano COLS
    sc_vector<sc_in<Link>>  in_W,  in_E;   // tamano ROWS
    sc_vector<sc_out<Link>> out_W, out_E;  // tamano ROWS

    cgra_mesh_static_detail::CellChain<0, COLS, CellTs...> pe;
    sc_vector<Router> router;

    SC_HAS_PROCESS(NoC_Mesh_Static);

    explicit NoC_Mesh_Static(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"),
          in_N("in_N"), in_S("in_S"), out_N("out_N"), out_S("out_S"),
          in_W("in_W"), in_E("in_E"), out_W("out_W"), out_E("out_E"),
          pe(),
          router("router")
    {
        in_N.init(COLS);  out_N.init(COLS);
        in_S.init(COLS);  out_S.init(COLS);
        in_W.init(ROWS);  out_W.init(ROWS);
        in_E.init(ROWS);  out_E.init(ROWS);

        instr_sig.init(ROWS * COLS);

        router.init(ROWS * COLS, [](const char* rname, auto idx) {
            return new Router(rname, (int)(idx / COLS), (int)(idx % COLS));
        });

        local_N_in.init(ROWS * COLS);  local_N_out.init(ROWS * COLS);
        local_S_in.init(ROWS * COLS);  local_S_out.init(ROWS * COLS);
        local_E_in.init(ROWS * COLS);  local_E_out.init(ROWS * COLS);
        local_W_in.init(ROWS * COLS);  local_W_out.init(ROWS * COLS);

        mesh_horiz_e.init(ROWS * (COLS > 1 ? COLS - 1 : 0));
        mesh_horiz_w.init(ROWS * (COLS > 1 ? COLS - 1 : 0));
        mesh_vert_s.init((ROWS > 1 ? ROWS - 1 : 0) * COLS);
        mesh_vert_n.init((ROWS > 1 ? ROWS - 1 : 0) * COLS);

        bnd_in_N.init(COLS);  bnd_out_N.init(COLS);
        bnd_in_S.init(COLS);  bnd_out_S.init(COLS);
        bnd_in_W.init(ROWS);  bnd_out_W.init(ROWS);
        bnd_in_E.init(ROWS);  bnd_out_E.init(ROWS);

        wire_all(std::make_index_sequence<sizeof...(CellTs)>{});

        SC_METHOD(bridge_boundary_in);
        for (int c = 0; c < COLS; c++) sensitive << in_N[c] << in_S[c];
        for (int r = 0; r < ROWS; r++) sensitive << in_W[r] << in_E[r];

        SC_METHOD(bridge_boundary_out);
        for (int c = 0; c < COLS; c++) sensitive << bnd_out_N[c] << bnd_out_S[c];
        for (int r = 0; r < ROWS; r++) sensitive << bnd_out_W[r] << bnd_out_E[r];
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

    template <std::size_t I>
    auto& cell() {
        return cgra_mesh_static_detail::Getter<I, 0, COLS, CellTs...>::get(pe);
    }

    void trace(sc_core::sc_trace_file* tf) const {
        trace_all(tf, std::make_index_sequence<sizeof...(CellTs)>{});
        for (int i = 0; i < ROWS * COLS; i++) router[i].trace(tf);
    }

private:
    sc_vector<sc_signal<InstrIn>> instr_sig;

    // Puertos locales router<->celda: tipo Link liso (espejo 1:1, ver
    // comentario de cabecera y NoC_Router.h -- el router arma/quita el
    // header, la celda de computo nunca ve un Packet).
    sc_vector<sc_signal<Link>> local_N_in, local_N_out;
    sc_vector<sc_signal<Link>> local_S_in, local_S_out;
    sc_vector<sc_signal<Link>> local_E_in, local_E_out;
    sc_vector<sc_signal<Link>> local_W_in, local_W_out;

    // Enlaces router<->router (mismo indexado que sig_horiz_*/sig_vert_* de
    // CGRA_Mesh_Static, pero de tipo Packet).
    sc_vector<sc_signal<Packet>> mesh_horiz_e, mesh_horiz_w;
    sc_vector<sc_signal<Packet>> mesh_vert_s, mesh_vert_n;

    // Puente de borde: bnd_in_* alimenta el in_* de malla de un router de
    // borde (envuelve el Link externo con dest=posicion de ese router);
    // bnd_out_* recibe el out_* de malla de ese mismo router (se desenvuelve
    // a Link liso hacia el puerto externo en bridge_boundary_out()).
    sc_vector<sc_signal<Packet>> bnd_in_N, bnd_out_N;
    sc_vector<sc_signal<Packet>> bnd_in_S, bnd_out_S;
    sc_vector<sc_signal<Packet>> bnd_in_W, bnd_out_W;
    sc_vector<sc_signal<Packet>> bnd_in_E, bnd_out_E;

    template <std::size_t... I>
    void wire_all(std::index_sequence<I...>) { (wire_one<I>(), ...); }

    template <std::size_t I>
    void wire_one() {
        constexpr int r = I / COLS;
        constexpr int c = I % COLS;
        auto& cell_ref = cgra_mesh_static_detail::Getter<I, 0, COLS, CellTs...>::get(pe);
        Router& rt = router[I];

        cell_ref.clk(clk);
        cell_ref.rst(rst);
        cell_ref.enable(enable);
        cell_ref.instr_in(instr_sig[I]);

        rt.clk(clk);
        rt.rst(rst);
        rt.enable(enable);

        // Local: celda.out_X -> router.in_L_X ; router.out_L_X -> celda.in_X.
        cell_ref.out_N(local_N_in[I]);   rt.in_L_N(local_N_in[I]);
        rt.out_L_N(local_N_out[I]);      cell_ref.in_N(local_N_out[I]);

        cell_ref.out_S(local_S_in[I]);   rt.in_L_S(local_S_in[I]);
        rt.out_L_S(local_S_out[I]);      cell_ref.in_S(local_S_out[I]);

        cell_ref.out_E(local_E_in[I]);   rt.in_L_E(local_E_in[I]);
        rt.out_L_E(local_E_out[I]);      cell_ref.in_E(local_E_out[I]);

        cell_ref.out_W(local_W_in[I]);   rt.in_L_W(local_W_in[I]);
        rt.out_L_W(local_W_out[I]);      cell_ref.in_W(local_W_out[I]);

        // Malla: router <-> router vecino, o router <-> puente de borde
        // (mismo patron de indexado que CGRA_Mesh_Static::wire_one).
        if constexpr (r == 0) {
            rt.in_N(bnd_in_N[c]); rt.out_N(bnd_out_N[c]);
        } else {
            rt.in_N(mesh_vert_s[(r - 1) * COLS + c]); rt.out_N(mesh_vert_n[(r - 1) * COLS + c]);
        }

        if constexpr (r == ROWS - 1) {
            rt.in_S(bnd_in_S[c]); rt.out_S(bnd_out_S[c]);
        } else {
            rt.in_S(mesh_vert_n[r * COLS + c]); rt.out_S(mesh_vert_s[r * COLS + c]);
        }

        if constexpr (c == 0) {
            rt.in_W(bnd_in_W[r]); rt.out_W(bnd_out_W[r]);
        } else {
            rt.in_W(mesh_horiz_e[r * (COLS - 1) + (c - 1)]); rt.out_W(mesh_horiz_w[r * (COLS - 1) + (c - 1)]);
        }

        if constexpr (c == COLS - 1) {
            rt.in_E(bnd_in_E[r]); rt.out_E(bnd_out_E[r]);
        } else {
            rt.in_E(mesh_horiz_w[r * (COLS - 1) + c]); rt.out_E(mesh_horiz_e[r * (COLS - 1) + c]);
        }
    }

    template <std::size_t... I>
    void trace_all(sc_core::sc_trace_file* tf, std::index_sequence<I...>) const {
        (cgra_mesh_static_detail::Getter<I, 0, COLS, CellTs...>::get(const_cast<decltype(pe)&>(pe)).trace(tf), ...);
    }

    // Envuelve cada Link externo entrante con dest=posicion del router de
    // borde que lo recibe (ver comentario de cabecera, "Puente de borde").
    void bridge_boundary_in() {
        for (int c = 0; c < COLS; c++) {
            Packet pn; pn.valid = true; pn.dest_row = 0;        pn.dest_col = c; pn.data = in_N[c].read();
            bnd_in_N[c].write(pn);
            Packet ps; ps.valid = true; ps.dest_row = ROWS - 1; ps.dest_col = c; ps.data = in_S[c].read();
            bnd_in_S[c].write(ps);
        }
        for (int r = 0; r < ROWS; r++) {
            Packet pw; pw.valid = true; pw.dest_row = r; pw.dest_col = 0;        pw.data = in_W[r].read();
            bnd_in_W[r].write(pw);
            Packet pe_; pe_.valid = true; pe_.dest_row = r; pe_.dest_col = COLS - 1; pe_.data = in_E[r].read();
            bnd_in_E[r].write(pe_);
        }
    }

    // Desenvuelve el payload de cada router de borde hacia el Link externo
    // (el header se descarta -- ya cumplio su proposito al llegar aca).
    void bridge_boundary_out() {
        for (int c = 0; c < COLS; c++) {
            out_N[c].write(bnd_out_N[c].read().data);
            out_S[c].write(bnd_out_S[c].read().data);
        }
        for (int r = 0; r < ROWS; r++) {
            out_W[r].write(bnd_out_W[r].read().data);
            out_E[r].write(bnd_out_E[r].read().data);
        }
    }
};

#endif // NOC_MESH_STATIC_H
