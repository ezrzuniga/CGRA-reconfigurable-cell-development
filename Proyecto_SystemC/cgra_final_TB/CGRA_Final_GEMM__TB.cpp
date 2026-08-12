// CGRA_Final_GEMM__TB.cpp
// GEMM 2x2 sobre la malla final 3x3 (cgra_final/CGRA_Final_Mesh.h). A
// diferencia de gemm_hls/GEMM_2x2_Mesh.h (una malla 2x2 dedicada, solo MAC,
// donde las 4 celdas son bordes reales) aca el bloque MAC sistolico
// (P00=(1,1), P01=(1,2), P10=(2,1), P11=(2,2)) queda EMBEBIDO en una malla
// mas grande -- P00 no toca ningun borde real. El programa de cada MAC (4
// slots: relay de A, relay de B, MAC, lectura de ACC) es identico al de
// GEMM_2x2_Mesh.h, salvo P10 que lee su resultado por el Sur (unico borde
// real que tiene aqui) en vez de por el Oeste.
//
// Caminos de entrada/salida (ver cgra_final/CGRA_Final_Mesh.h para el mapa
// completo de bordes reales del layout):
//   A: in_W[1] -> Routing(1,0) [ctx0, relay 0-ciclos] -> P00 -> (slot0) -> P01
//      in_W[2] -> Routing(2,0) [ctx0, relay 0-ciclos] -> P10 -> (slot0) -> P11
//   B: in_N[1] -> Vectorial(0,1) [relay registrado, 1 ciclo]  -> P00 -> (slot1) -> P10
//      in_N[2] -> Escalar(0,2)   [relay registrado, 1 ciclo]  -> P01 -> (slot1) -> P11
//   C[0][0]: P00 (slot3, MOV ACC->W) -> Routing(1,0) [ctx1, relay 0-ciclos] -> out_W[1]
//   C[0][1]: P01 (slot3, MOV ACC->E) -> out_E[1] (borde real directo)
//   C[1][0]: P10 (slot3, MOV ACC->S) -> out_S[1] (borde real directo)
//   C[1][1]: P11 (slot3, MOV ACC->E) -> out_E[2] (borde real directo)
//
// Como A llega por un relay de 0 ciclos (Routing) y B por uno de 1 ciclo
// (Vectorial/Escalar, celdas con ALU registrada), escribir A y B en el mismo
// ciclo ya los deja alineados donde hace falta: el slot0 de P00 (ejecuta en
// el primer ciclo de la ventana) solo necesita A, que ya esta disponible de
// inmediato; el slot1 (un ciclo despues) necesita B, que para entonces ya
// atraveso el relay registrado de Vectorial/Escalar. No hace falta
// desfasar la escritura de B.
//
// INSTR_MEM_SIZE de la malla final es 16 (no 4 como en GEMM_2x2_Mesh.h), asi
// que el programa de 4 slots se carga con relleno NOP hasta las 16
// direcciones (mismo truco que gemm_load_program pero generalizado): cargar
// las 16 direcciones, una por ciclo, deja el PC realineado exactamente en 0
// al terminar, sin depender de un pulso de rst para forzarlo (un pulso de
// rst SI hace falta entre casos para limpiar el acumulador, y ademas borra
// el banco de contextos de Routing -- por eso setup_or_resume() se llama de
// nuevo despues de cada rst).

#include <systemc.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include "../cgra_final/CGRA_Final_Mesh.h"
#include "../pe_hls/test_util.h"

typedef CGRA_Final_Mesh Mesh;
typedef CGRA_Final_Link Link;
typedef CGRA_Final_Instr Instr;

static const int ROWS = CGRA_FINAL_ROWS;
static const int COLS = CGRA_FINAL_COLS;
static const int PROG_LEN = CGRA_FINAL_INSTR_MEM_SIZE;  // 16: tamano real de instr_mem de cada MAC
// Slots por copia del programa espacial (ver build_gemm_program): PROG_LEN
// debe ser multiplo de PROG_SLOTS para que la copia se repita un numero
// entero de veces al rellenar instr_mem.
static const int PROG_SLOTS = 8;

// Coordenadas (fila,columna) de las 4 celdas MAC del bloque sistolico,
// indexadas igual que GEMM_2x2_Mesh.h: [i][j] = P_ij.
struct Coord { int row, col; };
static const Coord MAC_CELL[2][2] = {
    {{1, 1}, {1, 2}},
    {{2, 1}, {2, 2}}
};

//============================================================================
// Constructores de instruccion
//============================================================================
static Instr mov_instr(sc_uint<3> src, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MOV; i.src_a = src; i.dst = dst; return i;
}
static Instr mac_instr(sc_uint<3> src_a, sc_uint<3> src_b, sc_uint<3> dst) {
    Instr i; i.opcode = OP_MAC; i.src_a = src_a; i.src_b = src_b; i.dst = dst; return i;
}
static Instr clear_acc_instr() {
    Instr i; i.opcode = OP_MOV; i.src_a = SRC_IMM; i.imm = 0; i.dst = DST_ACC; return i;
}
static Instr nop_instr() { return Instr(); }

// Programa espacial de GEMM 2x2, 8 slots por celda (en vez de los 4 de
// gemm_hls/GEMM_2x2_Mesh.h). Con 4 slots, P01/P11 hacian su MAC en el slot1
// -- suficiente en la malla 2x2 dedicada, donde B llega por un borde real
// directo (0 ciclos), pero no aqui: en esta malla B llega por un relay
// registrado (Vectorial/Escalar, 1 ciclo) y A por hasta 2 saltos MAC-a-MAC
// (1 ciclo cada uno), asi que P11 en particular necesita margen para 2
// saltos de A (via P10) y 2 de B (via P01) antes de que su propio MAC pueda
// leerlos. Ademas, un borde real recien escrito por el testbench (in_W/in_N)
// no se ve reflejado en el primer ciclo de la ventana: sc_signal::write()
// hecho fuera de un proceso de reloj (en sc_main, entre dos advance_cycles())
// solo se aplica en la fase de update del PROXIMO sc_start(), asi que
// cualquier lector (Routing_Cell::route() combinacional, o el propio in_N/in_W
// de una celda) todavia ve el valor de la ventana anterior en el slot 0 y
// recien el nuevo en el slot 1 -- confirmado instrumentando PE_MAC_HLS::tick()
// con el pc/in_W/in_N real. Por eso cada relay que depende de un borde real
// arranca en addr1 (no addr0), un ciclo mas tarde que lo que sugeriria un
// relay "combinacional de 0 ciclos" en el papel. 8 slots dan margen de sobra
// a todas las celdas por igual con este corrimiento:
//   addr 1: P00/P10 relevan A (borde real ya estable) un salto hacia el este
//           (P01/P11).
//   addr 2: P00 releva B (su propio N, estable desde addr2) hacia P10; P01
//           releva B (su propio N, estable desde addr2) hacia P11.
//   addr 4: MAC de P00/P01/P10 (A y B con margen de al menos 1 ciclo cada uno).
//   addr 5: MAC de P11 (el operando mas lento, B via Escalar->P01->P11, dos
//           saltos registrados, recien esta listo desde addr3).
//   addr 6/7: lectura de ACC hacia el puerto de salida de cada celda.
static void build_gemm_program(Instr prog[2][2][PROG_SLOTS]) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int s = 0; s < PROG_SLOTS; s++)
                prog[i][j][s] = nop_instr();

    prog[0][0][1] = mov_instr(SRC_WEST, DST_EAST);                   // A -> P01
    prog[0][0][2] = mov_instr(SRC_NORTH, DST_SOUTH);                 // B -> P10
    prog[0][0][4] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][0][6] = mov_instr(SRC_ACC, DST_WEST);   // -> Routing(1,0) ctx1 -> out_W[1]

    prog[0][1][2] = mov_instr(SRC_NORTH, DST_SOUTH);                 // B -> P11
    prog[0][1][4] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[0][1][6] = mov_instr(SRC_ACC, DST_EAST);   // -> out_E[1]

    prog[1][0][1] = mov_instr(SRC_WEST, DST_EAST);                   // A -> P11
    prog[1][0][4] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][0][6] = mov_instr(SRC_ACC, DST_SOUTH);  // -> out_S[1] (borde real directo)

    prog[1][1][5] = mac_instr(SRC_WEST, SRC_NORTH, DST_ACC);
    prog[1][1][7] = mov_instr(SRC_ACC, DST_EAST);   // -> out_E[2]
}

static Instr routing_relay_in()  {  // sel_E = FROM_W: borde real W -> enlace interno hacia el bloque MAC
    // DEBUG: tambien sel_S=FROM_W (solo importa para Routing(2,0), que SI
    // tiene borde real S) para poder verificar por out_S[0] si el relay esta
    // realmente activo, sin depender de nada rio abajo.
    return make_routing_config_instr_hls<CGRA_FINAL_DATA_W>(RC_NONE, RC_FROM_W, RC_FROM_W, RC_NONE);
}
static Instr routing_relay_out() {  // sel_W = FROM_E: enlace interno (C[0][0] de P00) -> borde real W
    return make_routing_config_instr_hls<CGRA_FINAL_DATA_W>(RC_NONE, RC_NONE, RC_NONE, RC_FROM_E);
}

//============================================================================
// Carga de programas (relleno hasta PROG_LEN direcciones, una por ciclo --
// generaliza gemm_load_program()/gemm_load_clear_acc() de GEMM_2x2_Mesh.h a
// INSTR_MEM_SIZE=16: al terminar el bucle, el PC de las 4 celdas MAC queda
// realineado exactamente en 0, sin necesidad de un pulso de rst).
//============================================================================
// Carga el programa GEMM de las 4 MAC Y de paso los relays de entrada de
// Routing, dentro del MISMO bucle de 16 ciclos -- fusionar ambas cargas en
// una sola pasada evita que un advance_cycles() aparte para Routing corra el
// PC de las MAC un ciclo antes de empezar el bucle (eso desalinea slot0 con
// el primer tick de feed_k_step). El pulso de rst inicial dejar el PC en 0
// con certeza (y de paso limpia el banco de contextos de Routing, que este
// mismo bucle recarga en su primera iteracion) -- unico punto donde el
// programa espacial arranca desde un estado conocido, sin depender de
// cuantos ciclos hayan corrido antes.
static void load_mac_program(Mesh& mesh, sc_signal<bool>& rst, const Instr prog[2][2][PROG_SLOTS]) {
    rst.write(true);
    advance_cycles(1);
    rst.write(false);

    for (int addr = 0; addr < PROG_LEN; addr++) {
        if (addr == 0) {
            mesh.load_instr(2, 0, 0, routing_relay_in());  // Routing(2,0): A -> fila 2
            mesh.load_instr(1, 0, 0, routing_relay_in());  // Routing(1,0): A -> fila 1 (modo entrada)
        }
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                // El patron de 4 slots se repite 4 veces hasta llenar las 16
                // direcciones (en vez de rellenar con NOP): asi el programa
                // es periodico con periodo 4 (divisor de 16) y las MAC
                // ejecutan slot0..slot3 en bucle cerrado sin importar en que
                // fase este el PC -- no hace falta alinear nada con rst.
                mesh.load_instr(MAC_CELL[i][j].row, MAC_CELL[i][j].col, addr, prog[i][j][addr % PROG_SLOTS]);
            }
        advance_cycles(1);
    }
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            mesh.clear_instr(MAC_CELL[i][j].row, MAC_CELL[i][j].col);
    mesh.clear_instr(2, 0);
    mesh.clear_instr(1, 0);
}

static void clear_mac_acc(Mesh& mesh) {
    for (int addr = 0; addr < PROG_LEN; addr++) {
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                mesh.load_instr(MAC_CELL[i][j].row, MAC_CELL[i][j].col, addr, clear_acc_instr());
        advance_cycles(1);
    }
    // el PC ya dio la vuelta y quedo en 0 -- un ciclo mas para que el
    // clear_acc (ahora presente en las 16 direcciones) se ejecute de verdad.
    advance_cycles(1);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            mesh.clear_instr(MAC_CELL[i][j].row, MAC_CELL[i][j].col);
}

// Relays de B (Vectorial/Escalar): una unica instruccion, resistente a
// cualquier fase del PC porque se replica en las 16 direcciones -- se
// programan una sola vez, un pulso de rst posterior no les afecta (siguen
// siendo la misma instruccion sin importar donde caiga el PC).
static void setup_b_relays(Mesh& mesh) {
    Instr relay = mov_instr(SRC_NORTH, DST_SOUTH);
    for (int addr = 0; addr < PROG_LEN; addr++) {
        mesh.load_instr(0, 1, addr, relay);  // Vectorial
        mesh.load_instr(0, 2, addr, relay);  // Escalar
        advance_cycles(1);
    }
    mesh.clear_instr(0, 1);
    mesh.clear_instr(0, 2);
}

//============================================================================
// Un caso de prueba: A, B (2x2) y su etiqueta.
//============================================================================
struct GemmCase {
    std::string label;
    int32_t A[2][2];
    int32_t B[2][2];
};

static void print_matrix(std::ostream& os, const char* name, const int32_t M[2][2]) {
    os << "      " << name << " = [ " << std::setw(4) << M[0][0] << " " << std::setw(4) << M[0][1] << " ]\n"
       << "        " << std::string(strlen(name), ' ') << "   [ " << std::setw(4) << M[1][0] << " " << std::setw(4) << M[1][1] << " ]\n";
}

static void feed_k_step(sc_signal<bool>& rst, sc_signal<bool>& enable,
                         sc_signal<Link>* in_N, sc_signal<Link>* in_W,
                         sc_signal<Link>* out_E, sc_signal<Link>* out_S,
                         int32_t a_row0, int32_t a_row1, int32_t b_col0, int32_t b_col1) {
    in_W[1].write(Link({a_row0}));
    in_W[2].write(Link({a_row1}));
    in_N[1].write(Link({b_col0}));
    in_N[2].write(Link({b_col1}));
    cout << "  [DEBUG escritos] rst=" << rst.read() << " enable=" << enable.read()
         << " in_W[1]=" << in_W[1].read() << " in_N[2]=" << in_N[2].read() << "\n";
    for (int t = 0; t < PROG_SLOTS; t++) {
        advance_cycles(1);
        cout << "    [DEBUG tick " << t << "] in_W[1]=" << in_W[1].read() << " in_N[2]=" << in_N[2].read()
             << " out_S[0](Routing2,0 debug)=" << out_S[0].read()
             << " out_E[1]=" << out_E[1].read()
             << " out_S[1]=" << out_S[1].read() << " out_E[2]=" << out_E[2].read() << "\n";
    }
}

static bool run_case(Mesh& mesh, sc_signal<bool>& rst, sc_signal<bool>& enable, sc_signal<Link>* in_N, sc_signal<Link>* in_W,
                      sc_signal<Link>* out_W, sc_signal<Link>* out_E, sc_signal<Link>* out_S,
                      int case_num, const GemmCase& tc) {
    int32_t C[2][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            int32_t sum = 0;
            for (int k = 0; k < 2; k++) sum += tc.A[i][k] * tc.B[k][j];
            C[i][j] = sum;
        }

    cout << "\n============================================================\n"
         << "  CASO " << case_num << " -- " << tc.label << "\n"
         << "============================================================\n";
    print_matrix(cout, "A", tc.A);
    print_matrix(cout, "B", tc.B);
    cout << "      C esperado = [ " << std::setw(4) << C[0][0] << " " << std::setw(4) << C[0][1] << " ]\n"
         << "                   [ " << std::setw(4) << C[1][0] << " " << std::setw(4) << C[1][1] << " ]\n";

    test_section((std::string("Caso ") + std::to_string(case_num) + ": k=0").c_str());
    feed_k_step(rst, enable, in_N, in_W, out_E, out_S, tc.A[0][0], tc.A[1][0], tc.B[0][0], tc.B[0][1]);

    test_section((std::string("Caso ") + std::to_string(case_num) + ": k=1").c_str());
    feed_k_step(rst, enable, in_N, in_W, out_E, out_S, tc.A[0][1], tc.A[1][1], tc.B[1][0], tc.B[1][1]);

    // C[0][0]: P00 no toca ningun borde real -- exponerlo via Routing(1,0) ctx1.
    // Igual que un borde real recien escrito (ver build_gemm_program), un config
    // de Routing recien cargado tampoco se ve reflejado en el ciclo inmediato
    // siguiente al load_instr(): bridge_instr_in() -> config_bank (clk.pos()) ->
    // route() combinacional es la misma cadena de un ciclo extra. Un solo caso
    // (el primero) lo delata porque ctx1 arranca en su default (RC_NONE) --
    // los casos siguientes reutilizan un ctx1 que ya quedo asentado desde antes.
    mesh.load_instr(1, 0, 1, routing_relay_out());
    advance_cycles(2);
    mesh.clear_instr(1, 0);

    int32_t got[2][2] = {
        { (int32_t)out_W[1].read()[0], (int32_t)out_E[1].read()[0] },
        { (int32_t)out_S[1].read()[0], (int32_t)out_E[2].read()[0] }
    };

    bool ok = true;
    cout << "\n      C obtenido = [ " << std::setw(4) << got[0][0] << " " << std::setw(4) << got[0][1] << " ]\n"
         << "                   [ " << std::setw(4) << got[1][0] << " " << std::setw(4) << got[1][1] << " ]\n\n";
    static const char* labels[2][2] = {{"C[0][0] (out_W[1], via Routing)", "C[0][1] (out_E[1])"},
                                        {"C[1][0] (out_S[1])",             "C[1][1] (out_E[2])"}};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            std::ostringstream in;
            in << "A,B del caso " << case_num;
            test_check(ok, labels[i][j], in.str(), C[i][j], got[i][j]);
        }

    cout << "  Resultado del caso " << case_num << ": " << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

static void gen_random_matrix(int32_t M[2][2], int lo, int hi) {
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            M[i][j] = lo + std::rand() % (hi - lo + 1);
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

    Mesh mesh("mesh");
    mesh.clk(clk);
    mesh.rst(rst);
    mesh.enable(enable);
    for (int c = 0; c < COLS; c++) {
        mesh.in_N[c](in_N[c]);   mesh.out_N[c](out_N[c]);
        mesh.in_S[c](in_S[c]);   mesh.out_S[c](out_S[c]);
    }
    for (int r = 0; r < ROWS; r++) {
        mesh.in_W[r](in_W[r]);   mesh.out_W[r](out_W[r]);
        mesh.in_E[r](in_E[r]);   mesh.out_E[r](out_E[r]);
    }

    cout << "\n############################################################\n"
         << "#  GEMM 2x2 sobre CGRA_Final_Mesh (layout 3x3 final)         #\n"
         << "#  (0,0)=Memoria (0,1)=Vectorial (0,2)=Escalar               #\n"
         << "#  (1,0)=Routing (1,1)=MAC       (1,2)=MAC                   #\n"
         << "#  (2,0)=Routing (2,1)=MAC       (2,2)=MAC                   #\n"
         << "############################################################\n";

    test_section("Reset");
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    advance_cycles(2);
    rst.write(false);
    enable.write(true);

    Instr prog[2][2][PROG_SLOTS];
    build_gemm_program(prog);

    test_section("Programacion inicial: relays de B (Vectorial/Escalar) + relays de A y programa MAC (Routing + bloque sistolico)");
    setup_b_relays(mesh);
    load_mac_program(mesh, rst, prog);
    cout << "  [DEBUG] margen extra de " << PROG_LEN << " ciclos antes del primer caso\n";
    advance_cycles(PROG_LEN);

    // 3 matrices A/B de 2x2 con valores aleatorios en [-9, 9] (semilla fija
    // para que la corrida sea reproducible entre ejecuciones).
    std::srand(20260810);
    GemmCase cases[3];
    cases[0].label = "matrices aleatorias #1";
    cases[1].label = "matrices aleatorias #2";
    cases[2].label = "matrices aleatorias #3";
    for (int c = 0; c < 3; c++) {
        gen_random_matrix(cases[c].A, -9, 9);
        gen_random_matrix(cases[c].B, -9, 9);
    }

    bool all_ok = true;
    for (int c = 0; c < 3; c++) {
        if (c > 0) {
            test_section((std::string("Entre casos: limpiar acumuladores + recargar programa (caso ") +
                          std::to_string(c + 1) + ")").c_str());
            clear_mac_acc(mesh);
            load_mac_program(mesh, rst, prog);  // su propio pulso de rst realinea el PC y recarga Routing
        }
        bool ok = run_case(mesh, rst, enable, in_N, in_W, out_W, out_E, out_S, c + 1, cases[c]);
        all_ok = all_ok && ok;
    }

    cout << "\n############################################################\n";
    if (all_ok) {
        cout << "#  PASS: GEMM 2x2 correcto en los 3 casos sobre la CGRA final #\n";
    } else {
        cout << "#  FAIL: al menos un caso no coincidio (ver detalle arriba)  #\n";
    }
    cout << "############################################################\n";

    return all_ok ? 0 : 1;
}
