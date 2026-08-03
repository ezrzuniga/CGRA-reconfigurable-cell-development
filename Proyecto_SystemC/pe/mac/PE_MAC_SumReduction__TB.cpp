// PE_MAC_SumReduction__TB.cpp
// Reduccion por suma: sumar todos los elementos de un vector usando una sola
// PE_MAC standalone (sin malla). El vector no llega de una vez -- se transmite
// un elemento por ciclo por el borde in_W (mismo patron de streaming que la fila
// 2 de mesh/CGRA_Mesh_SmokeTest__TB.cpp), difundido a las 4 lanes por igual, y la
// PE acumula con OP_MAC(a=in_W, b=SRC_IMM=1, dst=DST_EAST): acc += a*1 = acc+=a,
// exponiendo la suma parcial en out_E en todo momento (ver PE_MAC.h::writeback,
// caso opcode==OP_MAC: r se reemplaza por acc ya actualizado antes de escribir
// dst, incluso cuando dst no es DST_ACC).
//
// No hace falta reprogramar la PE entre elementos: con INSTR_MEM_SIZE=1 la misma
// instruccion corre todos los ciclos (PC hace loop en el unico slot), asi que
// "sumar el vector" es simplemente: cargar la instruccion una vez, e ir
// escribiendo cada elemento nuevo en in_W un ciclo a la vez.

#include <systemc.h>
#include "PE_MAC.h"
#include "../pe_isa.h"
#include "../test_util.h"

typedef PE_VectorData<32, 4> Link;
typedef PE_VecInstrIn<32, 4> InstrIn;

static Link broadcast(sc_int<32> v) {
    Link r;
    for (int i = 0; i < 4; ++i) r[i] = v;
    return r;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N, in_S, in_E, in_W;
    sc_signal<Link> out_N, out_S, out_E, out_W;
    sc_signal<InstrIn> instr_in;

    PE_MAC<32, 4, 8, 1> pe("pe");  // INSTR_MEM_SIZE=1: 1 instruccion, corre todos los ciclos
    pe.clk(clk);
    pe.rst(rst);
    pe.enable(enable);
    pe.in_N(in_N); pe.in_S(in_S); pe.in_E(in_E); pe.in_W(in_W);
    pe.out_N(out_N); pe.out_S(out_S); pe.out_E(out_E); pe.out_W(out_W);
    pe.instr_in(instr_in);

    sc_trace_file* tf = sc_create_vcd_trace_file("pe_mac_sum_reduction_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, enable, "enable");
    sc_trace(tf, in_W, "in_W");
    sc_trace(tf, out_E, "out_E");
    pe.trace(tf);

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    in_N.write(Link()); in_S.write(Link()); in_E.write(Link()); in_W.write(Link());
    instr_in.write(InstrIn());
    advance_cycles(2);
    rst.write(false);
    enable.write(true);

    bool ok = true;

    test_section("Carga de instruccion: acc += a (OP_MAC, b=SRC_IMM=1, dst=DST_EAST)");
    InstrIn sum_instr;
    sum_instr.valid = true;
    sum_instr.addr = 0;
    sum_instr.instr.opcode = OP_MAC;
    sum_instr.instr.src_a = SRC_WEST;
    sum_instr.instr.src_b = SRC_IMM;
    sum_instr.instr.imm = 1;
    sum_instr.instr.dst = DST_EAST;
    instr_in.write(sum_instr);
    advance_cycles(1);
    sum_instr.valid = false;
    instr_in.write(sum_instr);
    advance_cycles(1);
    cout << "pe: acc += in_W cada ciclo, out_E = acumulador" << endl;

    // Vector a reducir: V = {6, -2, 9, 4, 0, 7, -5, 3}. Suma total = 22. Se
    // transmite un elemento por ciclo (broadcast a las 4 lanes), y se verifica
    // la suma parcial en dos puntos intermedios (no solo el resultado final)
    // para confirmar que la PE acumula elemento a elemento, no que "acerto" el
    // total por casualidad.
    static const int32_t V[] = {6, -2, 9, 4, 0, 7, -5, 3};
    static const int N = 8;

    test_section("Streaming del vector, 1 elemento por ciclo");
    int32_t running = 0;
    for (int i = 0; i < N; ++i) {
        running += V[i];
        in_W.write(broadcast(V[i]));
        advance_cycles(1);

        if (i == 2 || i == N - 1) {
            std::ostringstream in;
            in << "elementos consumidos: " << (i + 1) << "/" << N
               << ", ultimo=in_W=" << V[i];
            Link expected = broadcast(running);
            std::string label = (i == N - 1)
                ? "suma final del vector (los " + std::to_string(N) + " elementos)"
                : "suma parcial tras " + std::to_string(i + 1) + " elementos";
            test_check(ok, label, in.str(), expected, out_E.read());
        }
    }

    sc_close_vcd_trace_file(tf);
    if (ok) {
        cout << "\nPASS: reduccion por suma de un vector de " << N
             << " elementos via PE_MAC (acc += a, streaming por in_W)." << endl;
    }
    return ok ? 0 : 1;
}
