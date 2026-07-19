// PE_MAC__TB.cpp
// Smoke test standalone de PE_MAC: acumulacion de 1 ciclo (OP_MAC), freeze
// con enable=0, persistencia de acc a traves de rst, y reset explicito del
// acumulador via DST_ACC sin resetear la PE (leido de vuelta con SRC_ACC).

#include <systemc.h>
#include "PE_MAC.h"
#include "../pe_vector/pe_isa.h"

template <int DATA_W = 32, int VLEN = 4>
static PE_VectorData<DATA_W, VLEN> make_vector(const std::array<sc_int<DATA_W>, VLEN>& lanes) {
    PE_VectorData<DATA_W, VLEN> vec;
    for (int i = 0; i < VLEN; ++i) vec[i] = lanes[i];
    return vec;
}

template <int DATA_W, int VLEN>
static PE_VectorData<DATA_W, VLEN> lane_delta(const PE_VectorData<DATA_W, VLEN>& before,
                                               const PE_VectorData<DATA_W, VLEN>& after) {
    PE_VectorData<DATA_W, VLEN> d;
    for (int i = 0; i < VLEN; ++i) d[i] = after[i] - before[i];
    return d;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst;
    sc_signal<bool> enable;
    sc_signal<PE_VectorData<32, 4>> in_N, in_S, in_E, in_W;
    sc_signal<PE_VectorData<32, 4>> out_N, out_S, out_E, out_W;
    sc_signal<PE_VecInstrIn<32, 4>> instr_in;

    PE_MAC<32, 4, 8, 1> pe("pe"); // INSTR_MEM_SIZE=1: 1 instruccion, corre todos los ciclos
    pe.clk(clk);
    pe.rst(rst);
    pe.enable(enable);
    pe.in_N(in_N); pe.in_S(in_S); pe.in_E(in_E); pe.in_W(in_W);
    pe.out_N(out_N); pe.out_S(out_S); pe.out_E(out_E); pe.out_W(out_W);
    pe.instr_in(instr_in);

    sc_trace_file* tf = sc_create_vcd_trace_file("pe_mac_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, enable, "enable");
    sc_trace(tf, in_W, "in_W");
    sc_trace(tf, out_E, "out_E");
    pe.trace(tf);

    rst.write(true);
    enable.write(false);
    in_N.write(PE_VectorData<32, 4>());
    in_S.write(PE_VectorData<32, 4>());
    in_E.write(PE_VectorData<32, 4>());
    in_W.write(PE_VectorData<32, 4>());
    instr_in.write(PE_VecInstrIn<32, 4>());
    sc_start(20, SC_NS);

    rst.write(false);
    enable.write(true);
    in_W.write(make_vector<32, 4>({1, 2, 3, 4}));

    // load(): carga una instruccion en addr 0 y deja pasar 1 ciclo extra de
    // margen para que instr_mem quede estable antes de medir nada (el orden
    // relativo entre load_program()/issue() en el mismo flanco no esta
    // garantizado por SystemC, ver cgra_mesh/CLAUDE.md).
    auto load = [&](PE_VecInstrIn<32, 4> instr) {
        instr_in.write(instr);
        sc_start(10, SC_NS);
        instr.valid = false;
        instr_in.write(instr);
        sc_start(10, SC_NS);
    };

    PE_VecInstrIn<32, 4> mac_instr;
    mac_instr.valid = true;
    mac_instr.addr = 0;
    mac_instr.instr.opcode = OP_MAC;
    mac_instr.instr.src_a = SRC_WEST;
    mac_instr.instr.src_b = SRC_IMM;
    mac_instr.instr.imm = 5;
    mac_instr.instr.dst = DST_EAST;

    PE_VectorData<32, 4> expected_delta = make_vector<32, 4>({5, 10, 15, 20});
    bool all_pass = true;

    load(mac_instr);
    PE_VectorData<32, 4> prev = out_E.read();

    for (int i = 0; i < 3; ++i) {
        sc_start(10, SC_NS);
        PE_VectorData<32, 4> cur = out_E.read();
        PE_VectorData<32, 4> delta = lane_delta<32, 4>(prev, cur);
        if (delta != expected_delta) {
            cout << "FAIL delta ciclo " << i << ": got " << delta << ", expected " << expected_delta << endl;
            all_pass = false;
        } else {
            cout << "PASS delta 1 ciclo (no 3): " << delta << endl;
        }
        prev = cur;
    }

    enable.write(false);
    sc_start(30, SC_NS);
    PE_VectorData<32, 4> frozen = out_E.read();
    if (frozen != prev) {
        cout << "FAIL: enable=0 no congelo acc, out_E paso de " << prev << " a " << frozen << endl;
        all_pass = false;
    } else {
        cout << "PASS: enable=0 congela acc" << endl;
    }

    enable.write(true);
    sc_start(10, SC_NS);
    PE_VectorData<32, 4> after_resume = out_E.read();
    if (lane_delta<32, 4>(frozen, after_resume) != expected_delta) {
        cout << "FAIL delta tras reanudar: got " << lane_delta<32, 4>(frozen, after_resume)
             << ", expected " << expected_delta << endl;
        all_pass = false;
    } else {
        cout << "PASS delta tras reanudar: " << lane_delta<32, 4>(frozen, after_resume) << endl;
    }

    PE_VectorData<32, 4> before_rst = after_resume;
    rst.write(true);
    sc_start(10, SC_NS);
    rst.write(false);
    sc_start(10, SC_NS);
    PE_VectorData<32, 4> after_rst = out_E.read();
    if (lane_delta<32, 4>(before_rst, after_rst) != expected_delta) {
        cout << "FAIL: rst altero acc, delta got " << lane_delta<32, 4>(before_rst, after_rst)
             << ", expected " << expected_delta << endl;
        all_pass = false;
    } else {
        cout << "PASS: rst no limpia acc, acumulacion continua sin interrupcion" << endl;
    }

    // DST_ACC limpia el acumulador sin resetear la PE. clear_instr y
    // read_acc_instr son ambas idempotentes (MOV puro, no mutan acc salvo la
    // asignacion directa de clear_instr), asi que el resultado no depende de
    // cuantas veces load() las haya ejecutado de mas por la misma ambiguedad
    // de orden mencionada arriba.
    PE_VecInstrIn<32, 4> clear_instr;
    clear_instr.valid = true;
    clear_instr.addr = 0;
    clear_instr.instr.opcode = OP_MOV;
    clear_instr.instr.src_a = SRC_IMM;
    clear_instr.instr.imm = 0;
    clear_instr.instr.dst = DST_ACC;
    load(clear_instr);

    PE_VecInstrIn<32, 4> read_acc_instr;
    read_acc_instr.valid = true;
    read_acc_instr.addr = 0;
    read_acc_instr.instr.opcode = OP_MOV;
    read_acc_instr.instr.src_a = SRC_ACC;
    read_acc_instr.instr.dst = DST_EAST;
    load(read_acc_instr);

    PE_VectorData<32, 4> acc_after_clear = out_E.read();
    if (acc_after_clear != PE_VectorData<32, 4>()) {
        cout << "FAIL: DST_ACC no limpio el acumulador, SRC_ACC leyo " << acc_after_clear << endl;
        all_pass = false;
    } else {
        cout << "PASS: DST_ACC limpia el acumulador, SRC_ACC lo confirma en 0" << endl;
    }

    sc_close_vcd_trace_file(tf);
    return all_pass ? 0 : 1;
}
