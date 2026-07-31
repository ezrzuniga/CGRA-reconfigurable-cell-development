// GEMM_2x2_HLS_Top.h
// Top sintetizable para Vitis HLS: envuelve la malla real de GEMM_2x2_Mesh.h
// (CGRA_Mesh_Static<2,2,32,1,PE_MAC_Cell_HLS x4>, sin modificarla) detras de un
// limite de puertos escalares y una FSM propia que reemplaza, en hardware, lo
// que GEMM_2x2_MAC__TB.cpp hacia por C++ de simulacion (load_instr() +
// advance_cycles()). Ningun puerto de este top usa Link/PE_VectorData ni
// sc_vector de puertos -- eso queda adentro, en la conexion con la malla, para
// no exponer un tipo compuesto en el limite de sintesis.
//
// Puertos: A y B completas (matriz 2x2 cada una, latcheadas al ver `start`);
// la FSM decide sola que sublista corresponde a cada fase k=0,1 -- el host no
// necesita saber nada de fases, solo deja A/B estables y pulsa start.
//
// Estados: BOOT_LOAD(4) -> BOOT_CLEAR(1) -> IDLE(espera start) ->
//   CLEAR_ACC(4) -> REALIGN(1) -> RELOAD(4) -> PHASE0(4) -> PHASE1(4) ->
//   WAIT_DONE(1) -> DONE(1, vuelve a IDLE). CLEAR_ACC+REALIGN+RELOAD corren en
// cada `start` (no solo entre "casos" como en el testbench) para que cada
// invocacion sea independiente del historial: mismo resultado sin importar
// cuantas veces se corrio antes.
//
// Nota de temporizacion (la razon de ser de RELOAD/PHASE0's "adelanto" de un
// ciclo y de WAIT_DONE): toda escritura que este tick() hace hacia la malla
// (load_instr, mesh_rst_sig, los bordes de entrada) sale de un SC_METHOD
// sincrono -- igual que un registro, el efecto solo es visible para la malla
// un ciclo despues de emitido, nunca en el mismo ciclo. GEMM_2x2_MAC__TB.cpp
// no tiene ese problema porque escribe esas mismas senales directo desde
// sc_main (fuera de cualquier proceso), visibles de inmediato. Por eso
// RELOAD deja lista la entrada de fase0 en su propio ultimo ciclo (para que
// ya este presente el primer ciclo real de PHASE0) y PHASE0 hace lo mismo
// para fase1 -- sin este adelanto, las PEs P01/P11 (cuyo MAC depende de un
// relevo interno, no de un borde real) ejecutan con el operando de la fase
// anterior. Descubierto y corregido empiricamente contra
// GEMM_2x2_HLS_Top__TB.cpp -- ver ese archivo para los casos de prueba.

#ifndef GEMM_2X2_HLS_TOP_H
#define GEMM_2X2_HLS_TOP_H

#include <systemc.h>
#include "GEMM_2x2_Mesh.h"

class GEMM_2x2_HLS_Top : public sc_core::sc_module {
public:
    sc_in<bool> clk;
    sc_in<bool> rst;
    sc_in<bool> enable;
    sc_in<bool> start;
    sc_out<bool> done;

    sc_in<sc_int<32>> a00, a01, a10, a11;
    sc_in<sc_int<32>> b00, b01, b10, b11;
    sc_out<sc_int<32>> c00, c01, c10, c11;

    SC_HAS_PROCESS(GEMM_2x2_HLS_Top);

    explicit GEMM_2x2_HLS_Top(sc_core::sc_module_name name)
        : sc_module(name),
          clk("clk"), rst("rst"), enable("enable"), start("start"), done("done"),
          a00("a00"), a01("a01"), a10("a10"), a11("a11"),
          b00("b00"), b01("b01"), b10("b10"), b11("b11"),
          c00("c00"), c01("c01"), c10("c10"), c11("c11"),
          mesh("mesh"),
          state(ST_BOOT_LOAD), cnt(0)
    {
        gemm_program(prog);

        mesh.clk(clk);
        mesh.rst(mesh_rst_sig);
        mesh.enable(enable);
        mesh.in_N[0](in_N0_sig); mesh.out_N[0](out_N0_sig);
        mesh.in_N[1](in_N1_sig); mesh.out_N[1](out_N1_sig);
        mesh.in_S[0](in_S0_sig); mesh.out_S[0](out_S0_sig);
        mesh.in_S[1](in_S1_sig); mesh.out_S[1](out_S1_sig);
        mesh.in_W[0](in_W0_sig); mesh.out_W[0](out_W0_sig);
        mesh.in_W[1](in_W1_sig); mesh.out_W[1](out_W1_sig);
        mesh.in_E[0](in_E0_sig); mesh.out_E[0](out_E0_sig);
        mesh.in_E[1](in_E1_sig); mesh.out_E[1](out_E1_sig);

        SC_METHOD(tick);
        sensitive << clk.pos();
    }

private:
    enum State {
        ST_BOOT_LOAD, ST_BOOT_CLEAR, ST_IDLE,
        ST_CLEAR_ACC, ST_REALIGN, ST_RELOAD,
        ST_PHASE0, ST_PHASE1, ST_WAIT_DONE, ST_DONE
    };

    GemmMesh mesh;
    GemmInstr prog[GEMM_ROWS][GEMM_COLS][4];

    sc_signal<bool> mesh_rst_sig;
    sc_signal<GemmLink> in_N0_sig, in_N1_sig, out_N0_sig, out_N1_sig;
    sc_signal<GemmLink> in_S0_sig, in_S1_sig, out_S0_sig, out_S1_sig;
    sc_signal<GemmLink> in_W0_sig, in_W1_sig, out_W0_sig, out_W1_sig;
    sc_signal<GemmLink> in_E0_sig, in_E1_sig, out_E0_sig, out_E1_sig;

    sc_uint<4> state;
    sc_uint<3> cnt;

    // Matriz A/B latcheada al aceptar start -- separada de los puertos de
    // entrada para que el host pueda cambiar a00..b11 mientras la FSM corre
    // sin afectar la corrida en curso.
    sc_int<32> ra00, ra01, ra10, ra11;
    sc_int<32> rb00, rb01, rb10, rb11;

    static GemmLink scalar_link(sc_int<32> v) {
        GemmLink l;
        l[0] = v;
        return l;
    }

    void present_phase(sc_int<32> a0, sc_int<32> a1, sc_int<32> b0, sc_int<32> b1) {
        in_W0_sig.write(scalar_link(a0));
        in_W1_sig.write(scalar_link(a1));
        in_N0_sig.write(scalar_link(b0));
        in_N1_sig.write(scalar_link(b1));
    }

    void tick() {
        if (rst.read()) {
            state = ST_BOOT_LOAD;
            cnt = 0;
            mesh_rst_sig.write(true);
            done.write(false);
            return;
        }
        if (!enable.read()) return;

        switch (state) {
            case ST_BOOT_LOAD:
                mesh_rst_sig.write(false);
                gemm_load_program(mesh, prog, cnt.to_uint());
                if (cnt == 3) { state = ST_BOOT_CLEAR; cnt = 0; }
                else cnt = cnt + 1;
                break;

            case ST_BOOT_CLEAR:
                gemm_clear_all_instr(mesh);
                state = ST_IDLE;
                break;

            case ST_IDLE:
                gemm_clear_all_instr(mesh);
                if (start.read()) {
                    ra00 = a00.read(); ra01 = a01.read();
                    ra10 = a10.read(); ra11 = a11.read();
                    rb00 = b00.read(); rb01 = b01.read();
                    rb10 = b10.read(); rb11 = b11.read();
                    done.write(false);
                    state = ST_CLEAR_ACC;
                    cnt = 0;
                }
                break;

            case ST_CLEAR_ACC:
                gemm_load_clear_acc(mesh, cnt.to_uint());
                if (cnt == 3) { state = ST_REALIGN; cnt = 0; }
                else cnt = cnt + 1;
                break;

            case ST_REALIGN:
                // Ciclo extra de clear_acc (ejecutarla de verdad) + pulso de
                // rst interno para reponer pc=0 sin tocar acc ni instr_mem --
                // mismo fix que GEMM_2x2_MAC__TB.cpp entre casos.
                mesh_rst_sig.write(true);
                state = ST_RELOAD;
                break;

            case ST_RELOAD:
                mesh_rst_sig.write(false);
                gemm_load_program(mesh, prog, cnt.to_uint());
                if (cnt == 3) {
                    // Deja lista la entrada de fase0 un ciclo antes de que la
                    // malla realmente empiece esa fase (nuestra propia
                    // escritura tarda 1 ciclo en llegar a la malla, igual que
                    // cualquier escritura hecha desde un proceso reloj -- ver
                    // plan de diseno / notas de depuracion).
                    present_phase(ra00, ra10, rb00, rb01);
                    state = ST_PHASE0; cnt = 0;
                } else {
                    cnt = cnt + 1;
                }
                break;

            case ST_PHASE0:
                if (cnt == 3) {
                    // Deja lista fase1 un ciclo antes de que la malla la
                    // empiece de verdad, misma razon que arriba.
                    present_phase(ra01, ra11, rb10, rb11);
                    state = ST_PHASE1; cnt = 0;
                } else {
                    present_phase(ra00, ra10, rb00, rb01);
                    cnt = cnt + 1;
                }
                break;

            case ST_PHASE1:
                present_phase(ra01, ra11, rb10, rb11);
                if (cnt == 3) { state = ST_WAIT_DONE; cnt = 0; }
                else cnt = cnt + 1;
                break;

            case ST_WAIT_DONE:
                // La ultima instruccion de fase1 (MOV ACC->salida) tambien
                // tarda 1 ciclo en reflejarse en out_W/out_E -- un ciclo de
                // margen antes de leerlos como definitivos.
                state = ST_DONE;
                break;

            case ST_DONE:
                c00.write(out_W0_sig.read()[0]);
                c01.write(out_E0_sig.read()[0]);
                c10.write(out_W1_sig.read()[0]);
                c11.write(out_E1_sig.read()[0]);
                done.write(true);
                // Solo ST_IDLE mira `start` -- si esta tambien lo hiciera, un
                // pulso de 1 ciclo podria ser consumido aca en vez de en
                // IDLE, dejando a IDLE esperando un start que ya no llega.
                state = ST_IDLE;
                break;

            default:
                state = ST_BOOT_LOAD;
                break;
        }
    }
};

#endif // GEMM_2X2_HLS_TOP_H
