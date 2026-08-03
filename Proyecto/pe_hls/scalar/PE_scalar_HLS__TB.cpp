// PE_scalar_HLS__TB.cpp
// Port 1:1 de pe/scalar/PE_scalar__TB.cpp sobre PE_scalar_HLS (core de un
// solo proceso reloj-a-reloj, sin valid_toggle). Mismo estimulo, mismas
// expectativas -- prueba de paridad funcional con el core original.

#include <systemc.h>
#include "PE_scalar_HLS.h"

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst;
    sc_signal<bool> enable;
    sc_signal<sc_int<32>> in_N, in_S, in_E, in_W;
    sc_signal<sc_int<32>> out_N, out_S, out_E, out_W;
    sc_signal<PE_InstrIn<32>> instr_in;

    PE_scalar_HLS<> pe("pe");
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

    // Reset
    rst.write(true);
    enable.write(false);
    in_N.write(0);
    in_S.write(0);
    in_E.write(0);
    in_W.write(0);
    instr_in.write(PE_InstrIn<32>());
    sc_start(20, SC_NS);

    // Salir de reset, fijar entradas
    rst.write(false);
    enable.write(true);
    in_N.write(3);
    in_S.write(4);

    // Programar instr_mem[0]: out_E = in_N + in_S
    PE_InstrIn<32> load;
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

    // Congelar la PE antes de que el PC llegue a ejecutar el ADD: con
    // enable=0 no debe avanzar el PC ni producirse ninguna escritura en
    // ningun puerto de salida.
    enable.write(false);
    sc_start(50, SC_NS);
    if (out_E.read() != 0) {
        cout << "FAIL: enable=0 no congelo la ejecucion, out_E = " << out_E.read() << endl;
        return 1;
    }
    cout << "OK: enable=0 congela la PE (out_E sigue en " << out_E.read() << ")" << endl;

    // Reanudar: el PC debe seguir avanzando desde donde se congelo hasta
    // completar una vuelta y ejecutar el ADD en instr_mem[0].
    enable.write(true);
    sc_start(160, SC_NS);

    cout << "out_E = " << out_E.read() << endl;
    if (out_E.read() != 7 || out_N.read() != 0 || out_S.read() != 0 || out_W.read() != 0) {
        cout << "FAIL: routing direccional incorrecto (out_N=" << out_N.read()
             << " out_S=" << out_S.read() << " out_E=" << out_E.read()
             << " out_W=" << out_W.read() << ")" << endl;
        return 1;
    }
    cout << "PASS: out_E == in_N + in_S, y el resto de salidas no se tocaron" << endl;

    // rst debe reiniciar el PC a 0 sin borrar la memoria de instrucciones ya
    // programada: tras salir de reset, out_E debe volver a dar 7.
    rst.write(true);
    sc_start(10, SC_NS);
    rst.write(false);
    sc_start(160, SC_NS);

    if (out_E.read() != 7) {
        cout << "FAIL post-reset: expected 7, got " << out_E.read() << endl;
        return 1;
    }
    cout << "PASS post-reset: out_E == in_N + in_S" << endl;

    // Reprogramar instr_mem[0] con DST_ALL: el resultado debe difundirse a
    // los 4 vecinos en el mismo ciclo.
    in_N.write(5);
    in_S.write(6);
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

    if (out_N.read() != 11 || out_S.read() != 11 || out_E.read() != 11 || out_W.read() != 11) {
        cout << "FAIL broadcast: esperado 11 en los 4 puertos, obtuve"
             << " out_N=" << out_N.read() << " out_S=" << out_S.read()
             << " out_E=" << out_E.read() << " out_W=" << out_W.read() << endl;
        return 1;
    }
    cout << "PASS broadcast: out_N == out_S == out_E == out_W == in_N + in_S (DST_ALL)" << endl;

    return 0;
}
