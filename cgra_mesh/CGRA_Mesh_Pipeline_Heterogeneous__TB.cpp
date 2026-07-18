// CGRA_Mesh_Pipeline_Heterogeneous__TB.cpp
// Malla 3x3 heterogenea: fila 0 reproduce el pipeline escalar de
// CGRA_Mesh_Pipeline__TB.cpp (mismo resultado g=4), fila 1 corre un acumulador MAC
// vectorial real (acc += a * k) en pe(1,0) -- primer programa multi-instruccion de
// todo el repo, y primer uso de SRC_REG/DST_REG en cgra_mesh/:
//   pe(0,0) escalar: c = a + b   (a = in_W[0], b = in_N[0])
//   pe(0,1) escalar: e = c * d   (d = in_N[1])
//   pe(0,2) escalar: g = e & f   (f = in_N[2])            -> out_E[0] = broadcast(4)
//
//   pe(1,0) vectorial, programa ciclico de 3 instrucciones (INSTR_MEM_SIZE=3):
//     addr0  OP_MUL  tmp = a * k        (a = in_W[1] constante, k = SRC_IMM)
//     addr1  OP_ADD  acc = tmp + acc    (SRC_REG/DST_REG -- el registro se lee Y
//                                        escribe en la misma instruccion, acumulando)
//     addr2  OP_MOV  out_E = acc        (drena el acumulador a un puerto: un registro
//                                        interno no es observable desde fuera de la
//                                        PE hasta que una instruccion lo copia a un
//                                        puerto)
//   Cada 3 ciclos el acumulador crece en P = a*k. pe(1,0) es la columna oeste de la
//   malla, asi que su DST_EAST cae en un enlace interno hacia pe(1,1), no en el borde
//   out_E[1] -- pe(1,1)/pe(1,2) relevan el valor (MOV puro, SRC_WEST->DST_EAST, sin
//   computo propio) para que llegue al borde este observable desde el testbench.
//   Fila 2 queda sin programar (NOP), fuera de alcance -- alcance minimo a proposito,
//   primer caso de registro interno en este repo.
//
// Bug real encontrado al implementar esto (corregido en pe_scalar/ y pe_vector/, ver
// CLAUDE.md de cada carpeta): writeback() estaba escrito `sensitive << sig_alu_valid
// << sig_alu_result;` -- sc_signal::write() no genera evento cuando el valor nuevo
// coincide con el actual, asi que si dos instrucciones consecutivas producian el
// mismo numero (exactamente el caso de "acc = tmp + acc" cuando acc todavia vale 0),
// la escritura al registro se perdia en silencio. Se agrego un puerto
// `valid_toggle` a ALU_scalar/ALU_vector que se invierte en cada compute() habilitado
// sin importar el valor, y writeback() ahora es sensible solo a ese toggle -- ningun
// test anterior lo disparaba porque ninguno reusaba un registro entre instrucciones.
//
// INSTR_MEM_SIZE pasa de 1 a 3 para TODA la malla (es un parametro de template unico,
// compartido por todas las celdas). Las celdas de la fila 0 no tienen un segundo
// registro que llenar, asi que se carga la MISMA instruccion en las 3 direcciones --
// asi el resultado no depende de en que direccion este `pc` en cada ciclo, y el
// comportamiento de la fila 0 quedar igual de determinista que con
// INSTR_MEM_SIZE=1.
//
// Nota de diseno importante: ningun test anterior en cgra_mesh/ uso INSTR_MEM_SIZE>1,
// asi que este es el primer lugar donde el orden relativo entre los SC_METHOD
// pc_update() e issue() (ambos sensibles a clk.pos(), sin orden garantizado por la
// norma SystemC) podria importar -- con INSTR_MEM_SIZE=1 nunca importaba porque pc
// jamas cambiaba. Por eso el operando `a` se mantiene CONSTANTE durante toda la
// corrida (no streaming) y la verificacion es por DELTA entre dos instantes separados
// por un numero exacto de rotaciones (3*ROT ciclos), no por un valor absoluto
// calculado a mano desde t=0: el delta es exactamente 3*P sin importar en que fase
// absoluta cae cada instruccion, porque en estado estable pc rota con periodo 3 sin
// deriva. Esto tambien evita depender del transitorio de carga de instrucciones
// (durante el cual el acumulador puede sumar algo antes de que las 3 direcciones
// esten completamente cargadas).
//
// La fila 1 sigue sin poder usar SRC_NORTH como la fila 0 (in_N/in_S solo son borde
// real en la primera/ultima fila de la malla; en la fila 1 son enlaces internos hacia
// las filas 0 y 2, que acoplarian los dos pipelines).

#include <systemc.h>
#include "CGRA_Mesh_Heterogeneous.h"

static const int ROWS = 3;
static const int COLS = 3;
typedef CGRA_Mesh_Heterogeneous<ROWS, COLS, 32, 4, 8, 3> Mesh;
typedef Mesh::Link Link;

static const int R_ACC = 0;
static const int R_TMP = 1;

static Link lane_delta(const Link& before, const Link& after) {
    Link d;
    for (int i = 0; i < 4; i++) d[i] = after[i] - before[i];
    return d;
}

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst, enable;
    sc_signal<Link> in_N[COLS], out_N[COLS];
    sc_signal<Link> in_S[COLS], out_S[COLS];
    sc_signal<Link> in_W[ROWS], out_W[ROWS];
    sc_signal<Link> in_E[ROWS], out_E[ROWS];

    std::vector<CellKind> layout = {
        CellKind::SCALAR, CellKind::SCALAR, CellKind::SCALAR,  // fila 0
        CellKind::VECTOR, CellKind::VECTOR, CellKind::VECTOR,  // fila 1
        CellKind::SCALAR, CellKind::SCALAR, CellKind::SCALAR,  // fila 2 (sin programar)
    };
    Mesh mesh("mesh", layout);
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

    sc_trace_file* tf = sc_create_vcd_trace_file("cgra_mesh_pipeline_heterogeneous_wave");
    sc_trace(tf, clk, "clk");
    sc_trace(tf, rst, "rst");
    sc_trace(tf, enable, "enable");
    for (int c = 0; c < COLS; c++) {
        sc_trace(tf, in_N[c],  "in_N_" + std::to_string(c));
        sc_trace(tf, out_N[c], "out_N_" + std::to_string(c));
        sc_trace(tf, in_S[c],  "in_S_" + std::to_string(c));
        sc_trace(tf, out_S[c], "out_S_" + std::to_string(c));
    }
    for (int r = 0; r < ROWS; r++) {
        sc_trace(tf, in_W[r],  "in_W_" + std::to_string(r));
        sc_trace(tf, out_W[r], "out_W_" + std::to_string(r));
        sc_trace(tf, in_E[r],  "in_E_" + std::to_string(r));
        sc_trace(tf, out_E[r], "out_E_" + std::to_string(r));
    }
    mesh.trace(tf);

    // Reset
    rst.write(true);
    enable.write(false);
    for (int c = 0; c < COLS; c++) { in_N[c].write(Link()); in_S[c].write(Link()); }
    for (int r = 0; r < ROWS; r++) { in_W[r].write(Link()); in_E[r].write(Link()); }
    sc_start(20, SC_NS);

    rst.write(false);
    enable.write(true);

    // ---- Fila 0: mismo pipeline escalar que CGRA_Mesh_Pipeline__TB.cpp -------
    Mesh::Instr add_c;
    add_c.opcode = OP_ADD;
    add_c.src_a = SRC_WEST;
    add_c.src_b = SRC_NORTH;
    add_c.dst = DST_EAST;

    Mesh::Instr mul_e;
    mul_e.opcode = OP_MUL;
    mul_e.src_a = SRC_WEST;
    mul_e.src_b = SRC_NORTH;
    mul_e.dst = DST_EAST;

    Mesh::Instr and_g;
    and_g.opcode = OP_AND;
    and_g.src_a = SRC_WEST;
    and_g.src_b = SRC_NORTH;
    and_g.dst = DST_EAST;

    // ---- Fila 1: acumulador MAC vectorial, pe(1,0) -----------------------
    Mesh::Instr mac_mul;
    mac_mul.opcode = OP_MUL;
    mac_mul.src_a = SRC_WEST;
    mac_mul.src_b = SRC_IMM;
    mac_mul.imm = 5;          // k
    mac_mul.dst = DST_REG;
    mac_mul.reg_dst = R_TMP;

    Mesh::Instr mac_add;
    mac_add.opcode = OP_ADD;
    mac_add.src_a = SRC_REG;
    mac_add.reg_a = R_TMP;
    mac_add.src_b = SRC_REG;
    mac_add.reg_b = R_ACC;
    mac_add.dst = DST_REG;
    mac_add.reg_dst = R_ACC;   // acc = tmp + acc (acumula en su propio registro)

    Mesh::Instr mac_mov;
    mac_mov.opcode = OP_MOV;
    mac_mov.src_a = SRC_REG;
    mac_mov.reg_a = R_ACC;
    mac_mov.dst = DST_EAST;    // drena el acumulador al puerto de salida

    // pe(1,0) es la columna oeste de la malla: su DST_EAST cae en un enlace
    // INTERNO hacia pe(1,1), no en el borde out_E[1] de la malla (solo la
    // columna c==COLS-1 tiene out_E real). pe(1,1)/pe(1,2) relevan el valor
    // (MOV puro, SRC_WEST->DST_EAST) para que el resultado del MAC llegue al
    // borde este observable desde el testbench -- no agregan computo propio.
    Mesh::Instr relay;
    relay.opcode = OP_MOV;
    relay.src_a = SRC_WEST;
    relay.dst = DST_EAST;

    // Carga por columna de direccion: cada batch programa la direccion `addr` de las
    // 6 celdas activas a la vez (independientes entre si, un solo flanco de clk basta
    // para que latcheen juntas). Fila 0 y el relevo de fila 1 repiten su unica
    // instruccion real en las 3 direcciones; pe(1,0) recibe una instruccion distinta
    // por direccion (el programa MAC). Fila 2 no se programa (queda NOP).
    Mesh::Instr row0_instr[3] = {add_c, mul_e, and_g};
    Mesh::Instr mac_instr[3]  = {mac_mul, mac_add, mac_mov};
    for (int addr = 0; addr < 3; addr++) {
        mesh.load_instr(0, 0, addr, row0_instr[0]);
        mesh.load_instr(0, 1, addr, row0_instr[1]);
        mesh.load_instr(0, 2, addr, row0_instr[2]);
        mesh.load_instr(1, 0, addr, mac_instr[addr]);
        mesh.load_instr(1, 1, addr, relay);
        mesh.load_instr(1, 2, addr, relay);
        sc_start(10, SC_NS);
        mesh.clear_instr(0, 0);
        mesh.clear_instr(0, 1);
        mesh.clear_instr(0, 2);
        mesh.clear_instr(1, 0);
        mesh.clear_instr(1, 1);
        mesh.clear_instr(1, 2);
    }

    // Estimulo fila 0: a=5,b=7 -> c=12; d=3 -> e=36; f=15 -> g=36&15=4.
    // Escalar, difundido a las 4 lanes ya que alimenta celdas escalares.
    in_W[0].write(Link({5, 5, 5, 5}));
    in_N[0].write(Link({7, 7, 7, 7}));
    in_N[1].write(Link({3, 3, 3, 3}));
    in_N[2].write(Link({15, 15, 15, 15}));

    // Estimulo fila 1: a={1,2,3,4} (lanes no uniformes), CONSTANTE durante toda la
    // corrida -- cada rotacion del programa MAC suma P=a*k={5,10,15,20} al acumulador.
    in_W[1].write(Link({1, 2, 3, 4}));

    // Deja asentar la fila 0 y el transitorio de carga de la fila 1 (60ns = 6
    // ciclos, ya en estado estable de rotacion para el MAC).
    sc_start(60, SC_NS);

    bool ok = true;

    Link expected0({4, 4, 4, 4});
    Link got0 = out_E[0].read();
    cout << "out_E[0] = " << got0 << endl;
    if (got0 != expected0) {
        cout << "FAIL fila 0: esperaba " << expected0 << ", obtuve " << got0 << endl;
        ok = false;
    } else {
        cout << "PASS fila 0: pipeline escalar c=a+b, e=c*d, g=e&f produjo g=4 (broadcast) en out_E[0]" << endl;
    }

    Link snapshot1 = out_E[1].read();
    cout << "out_E[1] (snapshot 1, acumulador MAC) = " << snapshot1 << endl;

    // 3 rotaciones mas del programa MAC (9 ciclos = 90ns): el delta debe ser
    // exactamente 3*P, sin importar en que fase absoluta cayo cada instruccion.
    sc_start(90, SC_NS);

    Link snapshot2 = out_E[1].read();
    cout << "out_E[1] (snapshot 2, +3 rotaciones) = " << snapshot2 << endl;

    Link delta = lane_delta(snapshot1, snapshot2);
    Link expected_delta({15, 30, 45, 60});  // 3 * (a * k) = 3 * {5,10,15,20}
    cout << "delta acumulador (3 rotaciones) = " << delta << endl;
    if (delta != expected_delta) {
        cout << "FAIL fila 1: esperaba delta " << expected_delta << " tras 3 rotaciones, obtuve " << delta << endl;
        ok = false;
    } else {
        cout << "PASS fila 1: acumulador MAC (acc += a*k via SRC_REG/DST_REG) crecio "
             << expected_delta << " en 3 rotaciones del programa" << endl;
    }

    sc_close_vcd_trace_file(tf);
    return ok ? 0 : 1;
}
