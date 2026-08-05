// mesh_wrapper.h
// Puente TLM-2.0 <-> senales planas sobre una CGRA_Mesh_Heterogeneous<2,2> real,
// layout {Enrutamiento, Memoria, Escalar, Vectorial} -- el mismo arreglo del
// diagrama de nivel 2 (ver Entrega_Avance_2/images/lvl2_diagram.png y
// mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp, cuya mecanica de programacion
// esta clase reusa 1:1 pero orquestada desde el protocolo CSR en vez de un
// testbench). Expone un unico tlm_utils::simple_target_socket con el mismo mapa
// de registros que riscv_dma_main_mem_components/csr_dma.cpp ya asume del lado
// "cgra_socket" (ver README.md, seccion "Mapa de registros").
//
// clk/rst/enable del mesh son enteramente internos: MeshWrapper genera su propio
// sc_clock y se resetea una unica vez al arrancar la simulacion. Nadie fuera de este
// modulo necesita tocarlos -- toda la programacion/control pasa por target_socket.

#ifndef MESH_WRAPPER_H
#define MESH_WRAPPER_H

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <cstdint>

#include "../mesh/CGRA_Mesh_Heterogeneous.h"
#include "../pe/routing/PE_Routing_Cell.h"
#include "../memory/PE_Memory_Mesh_Cell.h"

// Catalogo de programas cargables via el registro CONFIG (0x00). El valor
// numerico de cada entrada coincide a proposito con el CGRA_KERNEL homonimo de
// riscv_dma_main_mem_components/cgra_kernel.h -- CONFIG no es un bitstream
// arbitrario, CSR_DMA reenvia cgra_config tal cual (ver csr_dma.cpp).
enum MeshProgram {
    // c = a + b, vía Celda Escalar + Celda Vectorial (Enrutamiento/Memoria
    // inactivas). Mismo resultado que la version <1,1> original -- preservado
    // para no romper el golden reference de RiscvCore::test_vector_add().
    PROGRAM_VECTOR_ADD = 0,

    // e = (a + b) * 2, recorriendo las 4 celdas del diagrama de nivel 2: b entra
    // por el borde norte real de Enrutamiento, se relevа hacia Memoria (NoC,
    // dir=NoC->SRAM), Memoria lo reenvia (dir=SRAM->NoC), Enrutamiento lo relay-ea
    // de vuelta hacia el sur (hacia Escalar); a entra directo por el borde oeste
    // real de Escalar. Escalar suma, Vectorial multiplica por 2 y expone el
    // resultado en su borde este real. Ver MeshWrapper::handle_start_write.
    PROGRAM_FULL_PIPELINE = 3,

    // Reduccion por suma: total = seed + sum(v[0..6]), tambien recorriendo las 4
    // celdas. INPUT_DATA_BUFFER (32 bytes) se reinterpreta como 8 enteros de 32
    // bits: word[0]=seed (viaja Enrutamiento->Memoria->Enrutamiento->Escalar,
    // igual que "b" en PROGRAM_FULL_PIPELINE) y word[1..7]=los 7 elementos del
    // vector a sumar (streameados uno por ciclo por el borde oeste real de
    // Escalar, acumulados con reg0 += west -- mismo patron que
    // pe/mac/PE_MAC_SumReduction__TB.cpp pero con el banco de registros de
    // Escalar en vez del acumulador dedicado de PE_MAC, que no existe en este
    // layout). Escalar reenvia el total a Vectorial, que lo expone en su borde
    // este real. Ver MeshWrapper::run_sum_reduction_dataflow.
    PROGRAM_SUM_REDUCTION = 4,

    // Multiplicacion matricial 2x2: C = A * B. INPUT_DATA_BUFFER (32 bytes) se
    // reinterpreta como 8 int32 row-major: word[0..3] = A = {A00,A01,A10,A11},
    // word[4..7] = B = {B00,B01,B10,B11}. La computa entera la celda Vectorial
    // aprovechando sus dos bordes reales per-lane (S y E): C aplanada row-major
    // {C00,C01,C10,C11} se arma como suma sobre k=0,1 de productos elementwise
    // lane-locked, un paso por cada columna de A / fila de B. Sin cruce entre
    // lanes ni acumulador dedicado: 8 multiplicaciones (4 lanes x 2 pasos) y 4
    // sumas, todo en la ALU vectorial. El resultado sale por el borde este real de
    // Vectorial (out_E_[1]), igual que los demas programas. Ver
    // MeshWrapper::run_matmul_dataflow.
    PROGRAM_MATMUL = 5,

    // e^u elementwise sobre los 4 lanes, en punto fijo Q4.12 (ver EXP_Q_* abajo).
    // INPUT_DATA_BUFFER (32 bytes) = 8 int32, de los cuales solo word[0..3] se usan
    // (los 4 valores de u); OUTPUT_DATA_BUFFER (16 bytes) = e^u por lane. Lo computa
    // entera la celda Vectorial (1,1): u entra por su borde sur real y el resultado
    // sale por su borde este real.
    //
    // Es el primer programa del catalogo que evalua una funcion trascendente, algo
    // que el ISA no ofrece (no hay exp, ni division, ni punto flotante). Se
    // construye con range reduction base-2:
    //
    //     e^u = 2^(u*log2(e)) = 2^k * 2^f     con k = floor(t) entero, f = t-k en [0,1)
    //
    // El 2^k es exactamente un OP_SRA -- y la ALU vectorial toma el shamt de b[i],
    // o sea POR LANE (ver ALU_vector.h), asi que los 4 lanes pueden desplazarse
    // cantidades distintas en una sola instruccion. El 2^f es un polinomio grado 3
    // evaluado por Horner con OP_MUL/OP_SRA/OP_ADD. Ver
    // MeshWrapper::run_exp_vec_dataflow para la secuencia completa.
    PROGRAM_EXP_VEC = 6,

    // Softmax sobre los 4 lanes en punto fijo Q4.12:
    //
    //     y_i = e^(x_i - max) / sum_j e^(x_j - max)
    //
    // INPUT_DATA_BUFFER usa word[0..3] (los 4 valores de x en Q4.12);
    // OUTPUT_DATA_BUFFER = y por lane, con sum(y) ~ 1.0 (== EXP_Q_ONE).
    //
    // Es el primer programa del catalogo que reparte el computo entre DOS celdas por
    // razones arquitectonicas y no por conveniencia:
    //
    //   Vectorial (1,1): todo lo lane-parallel -- las reducciones por butterfly
    //                    (max y suma), la resta del maximo, los 4 exponenciales en
    //                    paralelo y la normalizacion final.
    //   Escalar   (1,0): el reciproco 1/S por Newton-Raphson. S es UN numero, asi
    //                    que calcularlo en Vectorial serian 4 lanes haciendo el
    //                    mismo trabajo. Ademas la salida de Escalar hace broadcast
    //                    de lane 0 a las 4 lanes (ver pe/scalar/PE_Scalar_Cell.h),
    //                    que es exactamente como hay que devolverle r a Vectorial.
    //
    // Los dos escalares que softmax necesita difundir (el maximo y 1/S) encajan con
    // esa semantica de broadcast; la direccion dificil es la inversa (los 4 lanes de
    // Vectorial hacia Escalar, que solo ve lane 0), y por eso las reducciones se
    // hacen por butterfly DENTRO de Vectorial en vez de serializarlas hacia Escalar.
    //
    // Ver MeshWrapper::run_softmax_dataflow y la seccion "Como computa el softmax"
    // del README.
    PROGRAM_SOFTMAX = 7
};

// ---------------------------------------------------------------------------
// Formato de punto fijo de PROGRAM_EXP_VEC: Q4.12 (12 bits fraccionarios sobre
// int32). Un valor real v se representa como round(v * 4096).
//
// Por que Q4.12 y no Q16.16: OP_MUL trunca a DATA_W bits (r[i] = a[i]*b[i] sobre
// sc_int<32>, ver ALU_vector.h) -- no hay MULH, asi que el producto entero de dos
// Qm.f tiene que caber en 32 bits. En Q16.16 eso limitaria |a*b| < 0.5 (inservible);
// en Q4.12 el peor MUL de este kernel usa el 9% del rango de int32, o sea ~11x de
// margen (verificado sobre el rango completo de entrada).
static const int     EXP_Q_FRAC_BITS = 12;
static const int32_t EXP_Q_ONE       = 1 << EXP_Q_FRAC_BITS;   // 4096 == 1.0
static const int32_t EXP_Q_FRAC_MASK = EXP_Q_ONE - 1;          // aisla f = t - floor(t)

// log2(e) en Q4.12: round(1.4426950408889634 * 4096).
static const int32_t EXP_Q_LOG2E = 5909;

// Cota inferior de u. El kernel satura la entrada a [EXP_Q_U_MIN, 0] antes de operar
// -- no es una restriccion arbitraria sino el rango util del formato: e^-8 = 3.4e-4
// es ~1.4 LSB en Q4.12, debajo de eso el resultado es indistinguible de cero. La
// saturacion NO es cosmetica: sin ella, un u < -10.8 haria que shift >= 16 y, peor,
// un u > 0 daria un shift negativo -- y ALU_vector enmascara el shamt con
// (DATA_W-1), asi que ambos casos devolverian silenciosamente un valor absurdo en
// vez de saturar. Se implementa branchless (el ISA no tiene saltos): ver fases 2-3
// de run_exp_vec_dataflow.
static const int32_t EXP_Q_U_MIN = -8 * EXP_Q_ONE;             // -32768 == -8.0

// Coeficientes Horner de 2^f en [0,1), Q4.12: 2^f ~ c0 + c1*f + c2*f^2 + c3*f^3.
// Punto de partida: ajuste minimax sobre el ideal matematico (1.83 LSB de error
// maximo). Punto de llegada: esos coeficientes reoptimizados por descenso de
// coordenadas contra la implementacion en enteros REAL -- incluyendo el truncamiento
// de cada OP_SRA -- en vez de contra el ideal. Mismo conteo de instrucciones, error
// maximo 1.83 -> 1.25 LSB sobre todo el rango de entrada.
//
// c0 se dejo clavado en EXP_Q_ONE durante esa reoptimizacion: sale el mismo error
// maximo que dejandolo libre, y a cambio e^0 da exactamente 1.0 en vez de 1 LSB de
// mas. Una propiedad que conviene tener gratis, porque el caso u=0 no es un borde
// exotico -- es el lane del maximo en cada softmax.
//
// Grado 3 y no 4 a proposito: con grado 4 el truncamiento del paso extra de Horner
// pesa mas que el termino adicional y el error EMPEORA (2.56 LSB medidos). El
// polinomio no es el factor limitante aqui, la aritmetica de 12 bits si.
static const int32_t EXP_Q_C0 = 4096;
static const int32_t EXP_Q_C1 = 2856;
static const int32_t EXP_Q_C2 = 918;
static const int32_t EXP_Q_C3 = 324;

// ---------------------------------------------------------------------------
// Reciproco de PROGRAM_SOFTMAX: r = 1/S por Newton-Raphson, r <- r*(2 - S*r).
// Solo OP_MUL y OP_SUB, que es todo lo que hay -- el ISA no tiene division.
//
// Normalmente el problema de un reciproco por Newton-Raphson es acotar S para elegir
// una semilla, lo que pide un CLZ que este ISA no tiene. Aca sale gratis: como se
// resto el maximo, el lane del maximo aporta e^0 = exactamente EXP_Q_ONE (por eso se
// clavo EXP_Q_C0 en ONE) y los otros 3 terminos estan en (0,1]. Entonces
// S esta SIEMPRE en [1, 4] -- verificado ademas empiricamente sobre 50k vectores
// aleatorios. La resta del maximo no es solo estabilidad numerica aca: es lo que
// hace viable la division.
//
// Sobre ese rango se normaliza a [1,2] con un unico shift condicional (flag = S>=2,
// obtenido con OP_SLT, que devuelve 0/1 y sirve directo como shamt) antes de iterar.
// Newton-Raphson duplica los bits correctos por iteracion, asi que arrancar de un
// rango 2x en vez de 4x ahorra iteraciones: normalizar + 2 iteraciones da 1.01 LSB en
// 16 instrucciones, contra 1.05 LSB en 23 sin normalizar. Mejor en las dos
// dimensiones. El peor MUL usa el 0.84% de int32 (119x de margen).
static const int32_t SOFTMAX_NR_SEED_A = 5944;   // r0 = A - B*Sn, minimax sobre [1,2]
static const int32_t SOFTMAX_NR_SEED_B = 2007;
static const int     SOFTMAX_NR_ITERS  = 2;
static const int32_t EXP_Q_TWO         = 2 * EXP_Q_ONE;   // la constante "2" del NR

// Cantidad de elementos del vector a reducir en PROGRAM_SUM_REDUCTION (7, no 8:
// el primer word de INPUT_DATA_BUFFER es el seed, no un elemento del vector).
static const int SUM_REDUCTION_VECTOR_LEN = 7;

SC_MODULE(MeshWrapper) {
public:
    tlm_utils::simple_target_socket<MeshWrapper> target_socket;

    SC_CTOR(MeshWrapper);

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

    // Forwarding al trace() del mesh interno + las senales de borde, mismo estilo
    // que CGRA_Mesh_Heterogeneous::trace.
    void trace(sc_core::sc_trace_file* tf) const;

private:
    // INSTR_MEM_SIZE=1: el PC de cada PE es un contador libre que siempre
    // incrementa (no hay saltos/branch en el ISA), asi que "pc % INSTR_MEM_SIZE"
    // solo vale 0 todo el tiempo si INSTR_MEM_SIZE=1 -- mismo motivo documentado en
    // mesh/README.md, seccion "Programar las PEs".
    typedef CGRA_Mesh_Heterogeneous<2, 2, 32, 4, 8, 1> Mesh;
    typedef Mesh::Link  Link;
    typedef Mesh::Instr Instr;

    static const int ROWS = 2;
    static const int COLS = 2;

    // Layout row-major (index = row*COLS+col), identico al del diagrama de nivel 2:
    //   (0,0) Enrutamiento   (0,1) Memoria
    //   (1,0) Escalar        (1,1) Vectorial
    // Bordes reales de esta malla 2x2 (ver mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp
    // para el detalle de la adyacencia):
    //   Enrutamiento (0,0): N, W       Memoria (0,1): N, E
    //   Escalar      (1,0): S, W      Vectorial (1,1): S, E

    // ---- Reloj/control propios, no expuestos como puertos ------------------
    sc_core::sc_clock       clk_;
    sc_core::sc_signal<bool> rst_;
    sc_core::sc_signal<bool> enable_;
    sc_core::sc_event        reset_done_event_;
    bool                     reset_done_;

    // ---- Malla y sus bordes --------------------------------------------------
    Mesh mesh_;
    sc_core::sc_signal<Link> in_N_[COLS], out_N_[COLS];
    sc_core::sc_signal<Link> in_S_[COLS], out_S_[COLS];
    sc_core::sc_signal<Link> in_W_[ROWS], out_W_[ROWS];
    sc_core::sc_signal<Link> in_E_[ROWS], out_E_[ROWS];

    // ---- Registros CSR expuestos por target_socket --------------------------
    uint32_t config_;
    uint32_t start_;
    uint32_t status_;
    uint32_t done_;
    bool     programmed_;
    Link     result_;

    // Elementos crudos de PROGRAM_SUM_REDUCTION, guardados en handle_input_write
    // (llega como un unico bloque TLM de 32 bytes) para reproducirlos uno por
    // ciclo despues, en run_sum_reduction_dataflow (disparado recien por START).
    int32_t sum_reduction_seed_;
    int32_t sum_reduction_vec_[SUM_REDUCTION_VECTOR_LEN];

    // Operandos de PROGRAM_MATMUL, guardados en handle_input_write (llegan como un
    // unico bloque TLM de 32 bytes = 8 int32 row-major) para que
    // run_matmul_dataflow los reordene por lane recien en START. A row-major:
    // {A00,A01,A10,A11}; B row-major: {B00,B01,B10,B11}.
    int32_t matmul_a_[4];
    int32_t matmul_b_[4];

    // Los 4 valores de u (Q4.12) de PROGRAM_EXP_VEC, guardados en
    // handle_input_write para que run_exp_vec_dataflow los ponga en el borde sur
    // real de Vectorial recien en START (mismo criterio que matmul_a_/matmul_b_).
    int32_t exp_u_[4];

    // Los 4 valores de x (Q4.12) de PROGRAM_SOFTMAX, mismo criterio.
    int32_t softmax_x_[4];

    void reset_thread();

    void handle_config_write(uint32_t value);
    void handle_start_write(uint32_t value);
    void handle_input_write(const unsigned char* data, unsigned int len);
    void handle_output_read(unsigned char* data, unsigned int len);

    // Fases de PROGRAM_FULL_PIPELINE (ver .cpp): b viaja Enrutamiento->Memoria->
    // Enrutamiento->Escalar antes de que Escalar/Vectorial calculen el resultado.
    void run_full_pipeline_dataflow();

    // Fases de PROGRAM_SUM_REDUCTION (ver .cpp): el seed viaja Enrutamiento->
    // Memoria->Enrutamiento->Escalar (reg0=seed), despues Escalar acumula los 7
    // elementos de sum_reduction_vec_ uno por ciclo (reg0+=west) y reenvia el
    // total a Vectorial.
    void run_sum_reduction_dataflow();

    // Fases de PROGRAM_MATMUL (ver .cpp): en dos pasos (k=0,1) la celda Vectorial
    // multiplica elementwise los operandos ya reordenados por lane sobre sus
    // bordes reales S/E, acumula ambos productos en un registro vectorial y expone
    // C aplanada en su borde este real.
    void run_matmul_dataflow();

    // Fases de PROGRAM_EXP_VEC (ver .cpp): la celda Vectorial satura u a
    // [EXP_Q_U_MIN, 0], hace la range reduction base-2 (t = u*log2(e), k = floor(t),
    // f = t-k), evalua 2^f por Horner grado 3 y expone 2^f >> (-k) en su borde este
    // real. 23 instrucciones, todas sobre (1,1), todas lane-locked.
    void run_exp_vec_dataflow();

    // Fases de PROGRAM_SOFTMAX (ver .cpp): Vectorial hace las dos reducciones por
    // butterfly (max y suma), la resta del maximo, los 4 exponenciales y la
    // normalizacion final; Escalar calcula el reciproco 1/S por Newton-Raphson. Es
    // el unico programa del catalogo con ida y vuelta real sobre el enlace interno
    // Escalar<->Vectorial.
    void run_softmax_dataflow();

    // -----------------------------------------------------------------------
    // Constructores breves de instrucciones. Armar cada Instr campo por campo (el
    // estilo del resto del archivo) es apropiado cuando son 2 o 3; los kernels
    // aritmeticos son decenas de instrucciones seguidas y ahi la secuencia --que es
    // lo unico que importa entender-- se pierde entre el ruido. Todos delegan en
    // make_instr; los sufijos indican de donde sale cada operando:
    //   r = registro,  i = inmediato,  s = borde de malla (SRC_NORTH/SOUTH/EAST/WEST)
    // y los "out_" escriben a un borde (DST_*) en vez de a un registro.
    static Instr make_instr(int op, int src_a, int reg_a, int src_b, int reg_b,
                            int32_t imm, int dst, int reg_dst);
    static Instr alu_rr(int op, int ra, int rb, int rd);
    static Instr alu_ri(int op, int ra, int32_t imm, int rd);
    static Instr alu_ir(int op, int32_t imm, int rb, int rd);
    static Instr alu_sr(int op, int src, int rb, int rd);
    static Instr alu_ss(int op, int src_a, int src_b, int rd);
    static Instr alu_mov_imm(int32_t imm, int rd);
    static Instr alu_mov_src(int src, int rd);
    static Instr out_reg(int ra, int dst);
    static Instr out_rr(int op, int ra, int rb, int dst);
    static Instr out_ri(int op, int ra, int32_t imm, int dst);

    // Carga una instruccion en la celda (row,col) y le da SETTLE_CYCLES de margen.
    // Solo es seguro con instrucciones idempotentes (que escriban un registro/borde
    // que no lean) -- ver el comentario de run_exp_vec_dataflow.
    void step_cell(int row, int col, const Instr& instr);

    // Emite sobre Vectorial las 22 instrucciones de e^u en Q4.12, asumiendo que u ya
    // esta en reg_u y dejando el resultado en reg_out. Usa el resto del banco como
    // temporales. Compartida por PROGRAM_EXP_VEC y PROGRAM_SOFTMAX.
    void emit_exp_sequence(int reg_u, int reg_out);

    PE_Memory_Mesh_Cell<32, 4>& memory_cell();
};

#endif // MESH_WRAPPER_H
