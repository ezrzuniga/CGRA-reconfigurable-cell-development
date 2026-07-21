// Routing_Cell_PE_Integration__TB.cpp
// Test de integracion: valida que Routing_Cell (version de 8 puertos) puede
// insertarse entre una PE real (PE_Scalar_Cell) y la malla con conexion 1:1
// exacta -- PE.out_X <-> RC.in_L_X, RC.out_L_X <-> PE.in_X para las 4
// direcciones -- sin convenciones ad-hoc. A diferencia de la primera
// version de este test (single L port), aca PE_A escribe con un dst
// direccional real (DST_NORTH, no DST_ALL) y el dato de todas formas llega
// intacto a PE_B, porque cada direccion de la PE tiene su propio par
// in/out dedicado en la celda de enrutamiento.
//
// Camino: PE_A (dst=DST_NORTH) -> RC_A.in_L_N -> RC_A.out_E -> RC_B.in_W ->
// RC_B.out_L_N -> PE_B.in_N (src=SRC_NORTH) -> PE_B (dst=DST_ALL solo para
// poder leer el resultado desde el testbench por cualquier puerto).

#include <systemc.h>
#include "Routing_Cell.h"
#include "../pe/scalar/PE_Scalar_Cell.h"

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;

    typedef PE_VectorData<32, 4> Link;

    // ---- PE_A y PE_B -------------------------------------------------------
    PE_Scalar_Cell<32, 4, 8, 1> pe_a("pe_a"), pe_b("pe_b");
    sc_signal<Link> pe_a_in_N, pe_a_in_S, pe_a_in_E, pe_a_in_W;
    sc_signal<Link> pe_a_out_N, pe_a_out_S, pe_a_out_E, pe_a_out_W;
    sc_signal<Link> pe_b_in_N, pe_b_in_S, pe_b_in_E, pe_b_in_W;
    sc_signal<Link> pe_b_out_N, pe_b_out_S, pe_b_out_E, pe_b_out_W;
    sc_signal<PE_InstrIn<32>> pe_a_instr, pe_b_instr;

    pe_a.clk(clk); pe_a.rst(rst); pe_a.enable(enable);
    pe_a.in_N(pe_a_in_N); pe_a.in_S(pe_a_in_S); pe_a.in_E(pe_a_in_E); pe_a.in_W(pe_a_in_W);
    pe_a.out_N(pe_a_out_N); pe_a.out_S(pe_a_out_S); pe_a.out_E(pe_a_out_E); pe_a.out_W(pe_a_out_W);
    pe_a.instr_in(pe_a_instr);

    pe_b.clk(clk); pe_b.rst(rst); pe_b.enable(enable);
    pe_b.in_N(pe_b_in_N); pe_b.in_S(pe_b_in_S); pe_b.in_E(pe_b_in_E); pe_b.in_W(pe_b_in_W);
    pe_b.out_N(pe_b_out_N); pe_b.out_S(pe_b_out_S); pe_b.out_E(pe_b_out_E); pe_b.out_W(pe_b_out_W);
    pe_b.instr_in(pe_b_instr);

    // ---- RC_A y RC_B: mesh 1x2 de celdas de enrutamiento, unidas por E/W.
    //      Cada puerto local se conecta 1:1 con el puerto homonimo de su PE.
    Routing_Cell<32, 4> rc_a("rc_a"), rc_b("rc_b");
    sc_signal<Link> rc_link_e_to_w;  // RC_A.out_E -> RC_B.in_W
    sc_signal<Link> rc_link_w_to_e;  // RC_B.out_W -> RC_A.in_E
    sc_signal<Link> rc_a_mesh_in_N, rc_a_mesh_in_S, rc_a_mesh_out_N, rc_a_mesh_out_S;
    sc_signal<Link> rc_b_mesh_in_N, rc_b_mesh_in_S, rc_b_mesh_out_N, rc_b_mesh_out_S;
    sc_signal<Link> rc_a_dummy_in_W, rc_a_dummy_out_W;
    sc_signal<Link> rc_b_dummy_in_E, rc_b_dummy_out_E;
    sc_signal<RC_ConfigIn> rc_a_config_in, rc_b_config_in;
    sc_signal<sc_uint<2>> rc_a_ctx_sel, rc_b_ctx_sel;

    rc_a.clk(clk); rc_a.rst(rst); rc_a.enable(enable);
    rc_a.in_N(rc_a_mesh_in_N); rc_a.in_S(rc_a_mesh_in_S);
    rc_a.out_N(rc_a_mesh_out_N); rc_a.out_S(rc_a_mesh_out_S);
    rc_a.in_E(rc_link_w_to_e); rc_a.out_E(rc_link_e_to_w);
    rc_a.in_W(rc_a_dummy_in_W);    // W de RC_A no se usa en este test (mesh 1x2), pero
                                    // se cablea para que el puerto no quede flotante
    rc_a.out_W(rc_a_dummy_out_W);  // idem, no observado
    rc_a.in_L_N(pe_a_out_N); rc_a.out_L_N(pe_a_in_N);
    rc_a.in_L_S(pe_a_out_S); rc_a.out_L_S(pe_a_in_S);
    rc_a.in_L_E(pe_a_out_E); rc_a.out_L_E(pe_a_in_E);
    rc_a.in_L_W(pe_a_out_W); rc_a.out_L_W(pe_a_in_W);
    rc_a.config_in(rc_a_config_in); rc_a.ctx_sel(rc_a_ctx_sel);

    rc_b.clk(clk); rc_b.rst(rst); rc_b.enable(enable);
    rc_b.in_N(rc_b_mesh_in_N); rc_b.in_S(rc_b_mesh_in_S);
    rc_b.out_N(rc_b_mesh_out_N); rc_b.out_S(rc_b_mesh_out_S);
    rc_b.in_W(rc_link_e_to_w); rc_b.out_W(rc_link_w_to_e);
    rc_b.in_E(rc_b_dummy_in_E);    // E de RC_B no se usa en este test, cableado
                                    // para no dejar el puerto flotante
    rc_b.out_E(rc_b_dummy_out_E);  // idem, no observado
    rc_b.in_L_N(pe_b_out_N); rc_b.out_L_N(pe_b_in_N);
    rc_b.in_L_S(pe_b_out_S); rc_b.out_L_S(pe_b_in_S);
    rc_b.in_L_E(pe_b_out_E); rc_b.out_L_E(pe_b_in_E);
    rc_b.in_L_W(pe_b_out_W); rc_b.out_L_W(pe_b_in_W);
    rc_b.config_in(rc_b_config_in); rc_b.ctx_sel(rc_b_ctx_sel);

    sc_trace_file* tf = sc_create_vcd_trace_file("routing_cell_pe_integration_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, pe_a_out_N, "pe_a_out_N");
    sc_trace(tf, rc_link_e_to_w, "rc_link_e_to_w");
    sc_trace(tf, pe_b_in_N, "pe_b_in_N");
    sc_trace(tf, pe_b_out_N, "pe_b_out_N");
    rc_a.trace(tf); rc_b.trace(tf);
    pe_a.trace(tf); pe_b.trace(tf);

    bool all_pass = true;
    auto check = [&](const std::string& label, sc_int<32> got, sc_int<32> expected) {
        if (got != expected) {
            cout << "FAIL " << label << ": got " << got << ", expected " << expected << endl;
            all_pass = false;
        } else {
            cout << "PASS " << label << ": " << got << endl;
        }
    };

    // Reset.
    rst.write(true);
    enable.write(false);
    pe_a_instr.write(PE_InstrIn<32>());
    pe_b_instr.write(PE_InstrIn<32>());
    rc_a_config_in.write(RC_ConfigIn());
    rc_b_config_in.write(RC_ConfigIn());
    rc_a_ctx_sel.write(0);
    rc_b_ctx_sel.write(0);
    sc_start(20, SC_NS);

    rst.write(false);
    enable.write(true);

    // Config de ruteo: RC_A.E (mesh) <- L_N (lo que PE_A emite por out_N).
    //                  RC_B.L_N (hacia PE_B.in_N) <- W (mesh, lo que llega de RC_A).
    auto load_rc = [&](sc_signal<RC_ConfigIn>& port, RC_Config c) {
        RC_ConfigIn cfg; cfg.valid = true; cfg.ctx = 0; cfg.config = c;
        port.write(cfg);
        sc_start(10, SC_NS);
        cfg.valid = false;
        port.write(cfg);
        sc_start(10, SC_NS);
    };
    RC_Config cfg_a; cfg_a.sel_E = RC_FROM_LN;
    RC_Config cfg_b; cfg_b.sel_LN = RC_FROM_W;
    load_rc(rc_a_config_in, cfg_a);
    load_rc(rc_b_config_in, cfg_b);

    // Programa PE_A: imm=7 + reg[0](=0) = 7 -> DST_NORTH (dst direccional
    // real, no DST_ALL -- ya no hace falta la convencion de la version
    // anterior porque cada direccion de la PE tiene su propio puerto
    // dedicado en la celda de enrutamiento).
    auto load_pe = [&](sc_signal<PE_InstrIn<32>>& port, PE_InstrIn<32> instr) {
        port.write(instr);
        sc_start(10, SC_NS);
        instr.valid = false;
        port.write(instr);
        sc_start(10, SC_NS);
    };
    PE_InstrIn<32> add_instr;
    add_instr.valid = true;
    add_instr.addr = 0;
    add_instr.instr.opcode = OP_ADD;
    add_instr.instr.src_a = SRC_IMM;
    add_instr.instr.imm = 7;
    add_instr.instr.src_b = SRC_REG;
    add_instr.instr.dst = DST_NORTH;
    load_pe(pe_a_instr, add_instr);

    // Programa PE_B: MOV desde SRC_NORTH (lo que le entrega RC_B.out_L_N)
    // hacia DST_ALL, solo para poder leer el resultado en cualquier puerto
    // desde el testbench (conveniencia de observacion, no requisito de
    // diseno: cualquiera de los 4 puertos locales de RC_B funciona igual).
    PE_InstrIn<32> mov_instr;
    mov_instr.valid = true;
    mov_instr.addr = 0;
    mov_instr.instr.opcode = OP_MOV;
    mov_instr.instr.src_a = SRC_NORTH;
    mov_instr.instr.dst = DST_ALL;
    load_pe(pe_b_instr, mov_instr);

    // Un par de ciclos de margen para que el dato recorra PE_A -> RC_A -> RC_B -> PE_B.
    sc_start(30, SC_NS);

    check("PE_A emite 7+0=7 en out_N (DST_NORTH real)", pe_a_out_N.read().lane[0], 7);
    check("Dato llega a PE_B via RC_A->RC_B (SRC_NORTH)", pe_b_out_N.read().lane[0], 7);

    // Cambiar el immediate y reverificar que el camino sigue vivo ciclo a
    // ciclo (no es un artefacto de un solo pulso).
    add_instr.instr.imm = 3;
    load_pe(pe_a_instr, add_instr);
    sc_start(20, SC_NS);
    check("Segundo valor (3+0=3) tambien atraviesa el camino completo",
          pe_b_out_N.read().lane[0], 3);

    // Los otros 3 puertos locales de PE_A (S/E/W) no fueron escritos por
    // ninguna instruccion (dst siempre fue DST_NORTH o DST_ALL en PE_B) --
    // confirmar que esto NO contamina la salida por E (que en este test no
    // se usa como ruta real, pero antes hubiera sido indistinguible de un
    // "puerto local unico" con basura).
    check("out_S de PE_A no fue tocado por la instruccion DST_NORTH (queda en 0 inicial)",
          pe_a_out_S.read().lane[0], 0);

    sc_close_vcd_trace_file(tf);
    return all_pass ? 0 : 1;
}
