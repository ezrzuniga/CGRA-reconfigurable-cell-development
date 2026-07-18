// PE_vector__TB.cpp
// Smoke test minimo de los puertos de malla vectoriales N/S/E/W y de la PE
// vectorial: primero un routing direccional simple, luego broadcast DST_ALL.

#include <systemc.h>
#include "PE_vector.h"
#include "pe_isa.h"

template <int DATA_W = 32, int VLEN = 4>
static PE_VectorData<DATA_W, VLEN> make_vector(const std::array<sc_int<DATA_W>, VLEN>& lanes) {
    PE_VectorData<DATA_W, VLEN> vec;
    for (int i = 0; i < VLEN; ++i) {
        vec[i] = lanes[i];
    }
    return vec;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst;
    sc_signal<bool> enable;
    sc_signal<PE_VectorData<32, 4>> in_N, in_S, in_E, in_W;
    sc_signal<PE_VectorData<32, 4>> out_N, out_S, out_E, out_W;
    sc_signal<PE_VecInstrIn<32, 4>> instr_in;

    PE_vector<32, 4> pe("pe");
    pe.clk(clk);
    pe.rst(rst);
    pe.enable(enable);
    pe.in_N(in_N);
    pe.in_S(in_S);
    pe.in_E(in_E);
    pe.in_W(in_W);
    pe.out_N(out_N);
    pe.out_S(out_S);
    pe.out_E(out_E);
    pe.out_W(out_W);
    pe.instr_in(instr_in);

    sc_trace_file* tf = sc_create_vcd_trace_file("pe_vector_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, enable, "enable");
    sc_trace(tf, in_N, "in_N");
    sc_trace(tf, in_S, "in_S");
    sc_trace(tf, in_E, "in_E");
    sc_trace(tf, in_W, "in_W");
    sc_trace(tf, out_N, "out_N");
    sc_trace(tf, out_S, "out_S");
    sc_trace(tf, out_E, "out_E");
    sc_trace(tf, out_W, "out_W");
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
    in_N.write(make_vector<32, 4>({3, 4, 5, 6}));
    in_S.write(make_vector<32, 4>({4, 5, 6, 7}));

    PE_VecInstrIn<32, 4> load;
    load.valid = true;
    load.addr = 0;
    load.instr.opcode = OP_ADD;
    load.instr.src_a = SRC_NORTH;
    load.instr.src_b = SRC_SOUTH;
    load.instr.dst = DST_EAST;
    instr_in.write(load);
    sc_start(10, SC_NS);

    load.valid = false;
    instr_in.write(load);

    enable.write(false);
    sc_start(50, SC_NS);
    if (out_E.read() != PE_VectorData<32, 4>()) {
        cout << "FAIL: enable=0 no congelo la ejecucion, out_E = " << out_E.read() << endl;
        sc_close_vcd_trace_file(tf);
        return 1;
    }
    cout << "OK: enable=0 congela la PE" << endl;

    enable.write(true);
    sc_start(160, SC_NS);

    PE_VectorData<32, 4> expectedE = make_vector<32, 4>({7, 9, 11, 13});
    if (out_E.read() != expectedE || out_N.read() != PE_VectorData<32, 4>() ||
        out_S.read() != PE_VectorData<32, 4>() || out_W.read() != PE_VectorData<32, 4>()) {
        cout << "FAIL: routing direccional incorrecto" << endl;
        cout << "out_N=" << out_N.read() << " out_S=" << out_S.read()
             << " out_E=" << out_E.read() << " out_W=" << out_W.read() << endl;
        sc_close_vcd_trace_file(tf);
        return 1;
    }
    cout << "PASS: out_E == in_N + in_S" << endl;

    rst.write(true);
    sc_start(10, SC_NS);
    rst.write(false);
    sc_start(160, SC_NS);

    if (out_E.read() != expectedE) {
        cout << "FAIL post-reset: expected " << expectedE << ", got " << out_E.read() << endl;
        sc_close_vcd_trace_file(tf);
        return 1;
    }
    cout << "PASS post-reset: out_E == in_N + in_S" << endl;

    in_N.write(make_vector<32, 4>({5, 6, 7, 8}));
    in_S.write(make_vector<32, 4>({6, 7, 8, 9}));
    load.valid = true;
    load.addr = 0;
    load.instr.opcode = OP_ADD;
    load.instr.src_a = SRC_NORTH;
    load.instr.src_b = SRC_SOUTH;
    load.instr.dst = DST_ALL;
    instr_in.write(load);
    sc_start(10, SC_NS);

    load.valid = false;
    instr_in.write(load);
    sc_start(160, SC_NS);

    PE_VectorData<32, 4> expectedAll = make_vector<32, 4>({11, 13, 15, 17});
    if (out_N.read() != expectedAll || out_S.read() != expectedAll ||
        out_E.read() != expectedAll || out_W.read() != expectedAll) {
        cout << "FAIL broadcast: esperado " << expectedAll << " en los 4 puertos" << endl;
        cout << "out_N=" << out_N.read() << " out_S=" << out_S.read()
             << " out_E=" << out_E.read() << " out_W=" << out_W.read() << endl;
        sc_close_vcd_trace_file(tf);
        return 1;
    }
    cout << "PASS broadcast: out_N == out_S == out_E == out_W" << endl;

    sc_close_vcd_trace_file(tf);
    return 0;
}
