# Guía práctica: cómo instanciar y programar una CGRA nueva con este proyecto

Esta es una guía paso a paso para alguien que quiere **construir una aplicación nueva**
sobre las piezas de este repositorio: elegir qué celdas usar, armar la malla, escribir el
"programa espacial" que corre sobre ella, y llevarlo hasta un proyecto de Vitis HLS real.
Está escrita sobre el tier **C/HLS** (`Proyecto/*_hls_c/`, `Proyecto_HLS/`), que es el que
efectivamente sintetiza (ver `project.md`, sección 3.1, para por qué el tier SystemC no
sirve como entrada de síntesis).

Si querés entender la arquitectura en profundidad antes de construir algo, leé primero
`project.md`. Esta guía asume ese contexto y va directo a "cómo se hace".

## 0. Antes de empezar: las piezas disponibles

Cinco tipos de celda, todas intercambiables en cualquier posición de la malla porque
comparten el mismo contrato de puertos (`Link in_N/in_S/in_E/in_W`, `Link
out_N/out_S/out_E/out_W`, más `cell_step`/`cell_program`/`cell_clear_acc`):

| Celda | Header | Cuándo usarla |
|---|---|---|
| `PE_Scalar_State<DATA_W,VLEN,NUM_REGS,INSTR_MEM_SIZE>` | `pe_hls_c/scalar/PE_Scalar_HLS_C.h` | Cómputo escalar de propósito general (ALU RV32I-like) |
| `PE_Vector_State<...>` | `pe_hls_c/vector/PE_Vector_HLS_C.h` | Igual ISA, SIMD de `VLEN` lanes independientes (sin acumulador) |
| `PE_MAC_State<...>` | `pe_hls_c/mac/PE_MAC_HLS_C.h` | Igual que vectorial + acumulador direccionable de 1 ciclo (`acc += a*b`) — para GEMM/FFT/convoluciones |
| `Routing_Cell_State<DATA_W,VLEN>` | `pe_hls_c/routing/Routing_Cell_HLS_C.h` | Desviar tráfico entre celdas no vecinas en el flujo lógico del algoritmo; no calcula nada, solo conmuta |
| `PE_Memory_State<DATA_W,VLEN,SIZE_WORDS>` | `memory_hls_c/PE_Memory_HLS_C.h` | Un scratchpad local con DMA de ráfaga (SRAM↔NoC, SRAM↔SRAM) |

Todas viven bajo el mismo `Link = PE_VectorData<DATA_W,VLEN>` (arreglo fijo de `VLEN`
lanes de `DATA_W` bits) y el mismo `PE_Instruction<DATA_W>` (`pe_isa_hls_c.h`) como
"paquete de configuración", sin importar que el significado de los campos cambie por tipo
de celda (sección 2).

Genérico útil que ya existe y probablemente no necesitás reinventar:

- `mesh_hls_c/CGRA_Mesh_Static_C.h` — la malla misma (`CGRA_Mesh_Static_C<ROWS,COLS,
  DATA_W,VLEN,CellTs...>`, `mesh_step`/`mesh_program`/`mesh_clear_acc`/`mesh_read_outputs`).
- `cgra_hls_c/CGRA_Top_C.h` — el template `cgra_run<...>`, útil **solo si** tu aplicación
  tiene un horario de fases fijo conocido de antemano (como GEMM, sección 3).

## 1. Primera decisión: ¿fases fijas o control crudo?

Antes de escribir código, decidí cuál de los dos moldes ya construidos se ajusta a tu
aplicación — determina cómo se ve el "top" (paso 4):

- **Fases fijas** (`cgra_run<...>`, ver `gemm_hls_c/` como referencia completa): tu
  algoritmo tiene un número conocido de fases (`NUM_PHASES`), cada fase dura exactamente
  `INSTR_MEM_SIZE` ciclos (una pasada completa por la memoria de instrucciones de cada
  celda), y en cada fase se presenta un nuevo snapshot de los bordes de entrada. Encaja
  bien con algoritmos sistólicos/tipo tile (GEMM, convoluciones por bloques, FFT por
  etapas). El template hace toda la FSM por vos.
- **Control crudo** (`mesh_program`/`mesh_step` directo, ver
  `cgra_hetero_2x2_demo_c/` como referencia completa): tu aplicación no tiene un horario
  de fases uniforme, mezcla ráfagas de DMA de duración variable con cómputo, o
  simplemente todavía no sabés de antemano cuántos ciclos necesita cada etapa. El host
  (testbench o software) orquesta la secuencia de `mesh_step()`/`mesh_program()` a mano,
  ciclo por ciclo.

Si no estás seguro, empezá por control crudo — es estrictamente más simple de razonar (no
hay una FSM oculta) y siempre podés migrar a `cgra_run<...>` después si el horario
termina siendo fijo.

## 2. Receta paso a paso

### Paso 1 — Elegir el layout

Dibujá tu malla `ROWS x COLS` y anotá qué tipo de celda va en cada posición. Reglas del
wiring que importan al elegir el layout:

- Los bordes de la malla (fila 0, fila `ROWS-1`, columna 0, columna `COLS-1`) son los
  únicos puntos con `in_*`/`out_*` reales hacia el mundo exterior; el resto son enlaces
  internos entre celdas vecinas.
- La celda de memoria (`PE_Memory_State`) solo tiene puerto de NoC en el **borde oeste**
  (`in_W`/`out_W`) — sus puertos N/S/E siempre están en cero. Si necesitás que reciba o
  emita datos desde/hacia otra dirección, ponele una celda de enrutamiento como vecina en
  esa dirección, o ubicala de forma que su lado oeste sea el que necesitás.
- La celda de enrutamiento no calcula nada — solo tiene sentido como intermediaria entre
  dos celdas que necesitan comunicarse sin ser vecinas directas en el flujo lógico del
  algoritmo (p. ej. cruzar tráfico, o exponer el puerto oeste de una celda de memoria
  hacia una dirección distinta).

### Paso 2 — Los typedefs de celda y de malla

Un archivo nuevo (p. ej. `Proyecto/mi_app_hls_c/MiApp_Mesh_C.h`), mismo patrón que
`gemm_hls_c/GEMM_2x2_Mesh_C.h` o `cgra_hetero_2x2_demo_c/CGRA_Hetero_2x2_Demo_Top_C.h`:

```cpp
#include "../pe_hls_c/scalar/PE_Scalar_HLS_C.h"
#include "../pe_hls_c/routing/Routing_Cell_HLS_C.h"
#include "../memory_hls_c/PE_Memory_HLS_C.h"
#include "../mesh_hls_c/CGRA_Mesh_Static_C.h"

static const int MIAPP_ROWS = 1;
static const int MIAPP_COLS = 3;
static const int MIAPP_DATA_W = 32;
static const int MIAPP_VLEN = 1;

typedef PE_Scalar_State<MIAPP_DATA_W, MIAPP_VLEN, 8, 4>  MiAppScalarCell;
typedef Routing_Cell_State<MIAPP_DATA_W, MIAPP_VLEN>     MiAppRoutingCell;
typedef PE_Memory_State<MIAPP_DATA_W, MIAPP_VLEN, 64>    MiAppMemoryCell;

// El orden de CellTs... sigue el layout, fila por fila, columna por columna
// (posición I = fila*COLS + columna, ver Getter<> en CGRA_Mesh_Static_C.h).
typedef CGRA_Mesh_Static_C<MIAPP_ROWS, MIAPP_COLS, MIAPP_DATA_W, MIAPP_VLEN,
                            MiAppScalarCell, MiAppRoutingCell, MiAppMemoryCell>
        MiAppMesh_C;
typedef MiAppMesh_C::Link  MiAppLink_C;
typedef MiAppMesh_C::Instr MiAppInstr_C;
```

El `static_assert` dentro de `CGRA_Mesh_Static_C` te avisa en tiempo de compilación si el
número de tipos no coincide con `ROWS*COLS` — no hay forma de olvidarse una celda.

### Paso 3 — Diseñar el programa (cheat sheet de encoding por tipo de celda)

**Celdas tipo PE** (escalar/vectorial/MAC) — instrucciones de `pe_isa_hls_c.h`:

```cpp
MiAppInstr_C mov_imm;
mov_imm.opcode = OP_MOV; mov_imm.src_a = SRC_IMM; mov_imm.imm = 10;
mov_imm.dst = DST_REG;   mov_imm.reg_dst = 0;        // reg0 = 10

MiAppInstr_C add_west;
add_west.opcode = OP_ADD; add_west.src_a = SRC_REG; add_west.reg_a = 0;
add_west.src_b = SRC_WEST; add_west.dst = DST_EAST;  // out_E = reg0 + in_W
```

Opcodes disponibles: `ADD/SUB/AND/OR/XOR/MOV/SLL/SRL/SRA/SLT/SLTU/MUL` (+`MAC`, solo
`PE_MAC`). Fuentes (`src_a`/`src_b`): `SRC_REG/SRC_NORTH/SRC_SOUTH/SRC_EAST/SRC_WEST/
SRC_IMM` (+`SRC_ACC` en MAC). Destinos (`dst`): `DST_REG/DST_NORTH/DST_SOUTH/DST_EAST/
DST_WEST/DST_ALL` (+`DST_ACC` en MAC). Una instrucción sin tocar (`MiAppInstr_C()`,
constructor por defecto) es `OP_NOP` — ocupa un slot pero no hace nada, útil para rellenar
`INSTR_MEM_SIZE` cuando tu programa usa menos slots de los que declaraste.

**Celda de enrutamiento** — configuración, no instrucción de ALU:

```cpp
#include "../pe_hls_c/routing/Routing_Cell_HLS_C.h"
// Un mux por salida: cada out_X puede tomar cualquier in_Y, o quedar en 0 (RC_NONE).
MiAppInstr_C route_cfg = make_routing_config_instr_c<MIAPP_DATA_W>(
    /*sel_N=*/RC_NONE, /*sel_S=*/RC_NONE, /*sel_E=*/RC_FROM_W, /*sel_W=*/RC_FROM_E);
// sel_E=FROM_W y sel_W=FROM_E a la vez => paso bidireccional oeste<->este, cada ciclo.
```

Programar un contexto de enrutamiento **también lo activa de inmediato** — no hace falta
un paso aparte para "seleccionar" el contexto recién cargado.

**Celda de memoria** — campos de contexto + disparo, no instrucción de ALU:

```cpp
#include "../memory_hls_c/PE_Memory_HLS_C.h"
// slot = índice de contexto (0..3). Un campo por llamada.
make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_DST_ADDR, 0);   // dirección destino en SRAM
make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_COUNT, 1);      // 1 palabra
make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT);
make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_DIR, 1);        // 1 = NoC(oeste)->SRAM
make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_START, 0);      // dispara la rafaga sobre el contexto 0
```

Campos: `MEM_FIELD_SRC_ADDR/DST_ADDR/STRIDE/COUNT/MODE/DIR`, más el especial
`MEM_FIELD_START` que dispara la ráfaga configurada en ese contexto. `dir`:
`0`=SRAM→NoC(oeste), `1`=NoC(oeste)→SRAM, `2`=SRAM→SRAM. `mode`:
`AccessController::MODE_DIRECT` (1 palabra) o `MODE_STRIDE` (`count` palabras cada
`stride`).

### Paso 4 — El top: declaración + definición separadas

Vitis HLS **no reconoce un top marcado `inline`** (falla con `ERROR: [HLS 214-157] Top
function not found` si el `.cpp` del diseño solo hace `#include` de un header con el
cuerpo completo) — siempre separá declaración (`.h`) de definición (`.cpp`), mismo patrón
en los 2 tops ya existentes.

**Si tu aplicación tiene fases fijas** (`.h`):

```cpp
#include "MiApp_Mesh_C.h"

void MiApp_Top(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    MiAppInstr_C prog_instr, bool start, bool& done,
    MiAppLink_C in_N[MIAPP_NUM_PHASES][MIAPP_COLS], MiAppLink_C in_S[MIAPP_NUM_PHASES][MIAPP_COLS],
    MiAppLink_C in_W[MIAPP_NUM_PHASES][MIAPP_ROWS], MiAppLink_C in_E[MIAPP_NUM_PHASES][MIAPP_ROWS],
    MiAppLink_C out_N[MIAPP_COLS], MiAppLink_C out_S[MIAPP_COLS],
    MiAppLink_C out_W[MIAPP_ROWS], MiAppLink_C out_E[MIAPP_ROWS]);
```

y el `.cpp`:

```cpp
#include "MiApp_Top.h"
#include "../cgra_hls_c/CGRA_Top_C.h"

void MiApp_Top(/* mismos parametros */) {
    static MiAppMesh_C mesh;  // unico estado con memoria del diseno
    cgra_run<MIAPP_ROWS, MIAPP_COLS, MIAPP_DATA_W, MIAPP_VLEN,
             MIAPP_INSTR_MEM_SIZE, MIAPP_NUM_PHASES>(
        mesh, prog_valid, prog_row, prog_col, prog_slot, prog_instr, start, done,
        in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}
```

**Si tu aplicación usa control crudo** (`.h`, mismo patrón que
`CGRA_Hetero_2x2_Demo_Top_C.h`):

```cpp
void MiApp_Top(
    bool prog_valid, ap_uint<8> prog_row, ap_uint<8> prog_col, ap_uint<8> prog_slot,
    MiAppInstr_C prog_instr, bool rst, bool enable,
    MiAppLink_C in_N[MIAPP_COLS], MiAppLink_C in_S[MIAPP_COLS],
    MiAppLink_C in_W[MIAPP_ROWS], MiAppLink_C in_E[MIAPP_ROWS],
    MiAppLink_C out_N[MIAPP_COLS], MiAppLink_C out_S[MIAPP_COLS],
    MiAppLink_C out_W[MIAPP_ROWS], MiAppLink_C out_E[MIAPP_ROWS]);
```

y el `.cpp`:

```cpp
#include "MiApp_Top.h"

void MiApp_Top(/* mismos parametros */) {
    static MiAppMesh_C mesh;

    if (prog_valid) {
        mesh_program(mesh, prog_row, prog_col, prog_slot, prog_instr);
        return;
    }

    mesh_step(mesh, rst, enable, in_N, in_S, in_W, in_E);

    MiAppLink_C all_out_N[MIAPP_ROWS][MIAPP_COLS], all_out_S[MIAPP_ROWS][MIAPP_COLS];
    MiAppLink_C all_out_E[MIAPP_ROWS][MIAPP_COLS], all_out_W[MIAPP_ROWS][MIAPP_COLS];
    mesh_read_outputs(mesh, all_out_N, all_out_S, all_out_E, all_out_W);

    for (int c = 0; c < MIAPP_COLS; c++) {
#pragma HLS UNROLL
        out_N[c] = all_out_N[0][c];
        out_S[c] = all_out_S[MIAPP_ROWS - 1][c];
    }
    for (int r = 0; r < MIAPP_ROWS; r++) {
#pragma HLS UNROLL
        out_W[r] = all_out_W[r][0];
        out_E[r] = all_out_E[r][MIAPP_COLS - 1];
    }
}
```

El `static MiAppMesh_C mesh;` es la pieza clave que hace tu CGRA **reconfigurable de
verdad**: sobrevive entre invocaciones separadas del top, así que el host puede programar
la malla una sola vez y dispararla muchas veces con operandos distintos.

### Paso 5 — Testbench standalone en g++ (antes de tocar Vitis)

Patrón de "programar, después correr" — mirá `CGRA_Hetero_2x2_Demo_Top_C__TB.cpp` para el
ejemplo completo real. Dos funciones de conveniencia hacen el código legible:

```cpp
static void step(bool rst, MiAppLink_C in_N[MIAPP_COLS], MiAppLink_C in_S[MIAPP_COLS],
                  MiAppLink_C in_W[MIAPP_ROWS], MiAppLink_C in_E[MIAPP_ROWS],
                  MiAppLink_C out_N[MIAPP_COLS], MiAppLink_C out_S[MIAPP_COLS],
                  MiAppLink_C out_W[MIAPP_ROWS], MiAppLink_C out_E[MIAPP_ROWS]) {
    MiAppInstr_C unused;
    MiApp_Top(/*prog_valid=*/false, 0, 0, 0, unused, rst, /*enable=*/true,
              in_N, in_S, in_W, in_E, out_N, out_S, out_W, out_E);
}

static void program(ap_uint<8> row, ap_uint<8> col, ap_uint<8> slot, const MiAppInstr_C& instr) {
    MiAppLink_C dN[MIAPP_COLS], dS[MIAPP_COLS], dW[MIAPP_ROWS], dE[MIAPP_ROWS];
    MiAppLink_C oN[MIAPP_COLS], oS[MIAPP_COLS], oW[MIAPP_ROWS], oE[MIAPP_ROWS];
    MiApp_Top(/*prog_valid=*/true, row, col, slot, instr, false, true, dN, dS, dW, dE, oN, oS, oW, oE);
}
```

**Disciplina de temporización que hay que respetar al escribir el `main()`** (la lección
más cara del proyecto — ver sección 4 de esta guía): cada celda ve las salidas de sus
vecinos tal como quedaron el ciclo **anterior**, nunca las de "este mismo ciclo". Eso
significa que después de programar una celda o cambiar un borde externo, hay que dejar
correr suficientes `step()` para que el dato se propague por cada salto de la cadena antes
de leer o disparar el siguiente paso — nunca asumas que un valor está listo en el mismo
ciclo en que lo presentaste. Cuando la duración exacta no es obvia (rutas con DMA o rutas
combinacional+PE mezcladas), es más seguro correr algunos ciclos "de más" (*priming*) y
verificar el valor esperado, que calcular al límite y arriesgarse a leer un dato viejo.

### Paso 6 — Proyecto Vitis HLS

Copiá la carpeta de un proyecto existente como plantilla —
`Proyecto_HLS/hls_vitis_cgra_2x2_heterogeneous_c/` si tu top es de control crudo,
`Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c/` si usa `cgra_run<...>`— y ajustá 3 cosas en
`run_hls.tcl`:

```tcl
set PROJECT_NAME "mi_app_prj"
set TOP_MODULE    "MiApp_Top"
# FPGA_PART / CLK_PERIOD_NS: dejalos si tu dispositivo objetivo es el mismo
```

y los 2 `add_files` (unidad de traducción del diseño + testbench):

```tcl
add_files mi_app_top_c.cpp -cflags "-std=c++17"
add_files -tb ../../Proyecto/mi_app_hls_c/MiApp_Top__TB.cpp -cflags "-std=c++17 -Wno-unknown-pragmas"
```

`mi_app_top_c.cpp` es una unidad de traducción mínima que solo incluye tu `.cpp` real
(mismo patrón que `cgra_hetero_2x2_top_c.cpp`):

```cpp
#include "../../Proyecto/mi_app_hls_c/MiApp_Top.cpp"
```

### Paso 7 — Validar, en este orden

1. **g++ standalone**, sin Vitis: `g++ -std=c++17 -I<ruta a ap_int.h de tu instalacion de
   Vitis> MiApp_Top.cpp MiApp_Top__TB.cpp -o miapp_test && ./miapp_test`. Rápido, sin
   licencia, confirma la lógica.
2. **Vitis HLS real**, desde la carpeta del proyecto:
   ```bash
   source /path/to/Xilinx/Vitis_HLS/2024.1/settings64.sh
   vitis-run --mode hls --tcl run_hls.tcl
   ```
   Corre `csim_design -> csynth_design -> cosim_design -> export_design`. **No des por
   sintetizable un diseño que solo pasó `csim_design`** — el frontend de síntesis
   (`csynth_design`) es un parser distinto y más estricto (sección 4, primer punto);
   `csim_design` corre con g++ real y acepta cosas que `csynth_design` rechaza.
3. Mirá `csynth.rpt` en `<proyecto>/solution1/syn/report/` para latencia y utilización de
   recursos (BRAM/DSP/FF/LUT) — `project.md` sección 8 tiene ejemplos reales de referencia
   para comparar magnitudes.

## 3. Ejemplo completo trabajado: mini-CGRA 1x3 (Scalar + Routing + Memoria)

Un ejemplo ilustrativo, chico y completo, que combina las 3 piezas menos triviales
(cómputo, enrutamiento, memoria) en una sola malla. Sigue exactamente los mismos patrones
ya validados en `cgra_hetero_2x2_demo_c/` — antes de confiarlo, compilalo y corré su
testbench standalone (paso 7.1) como con cualquier componente nuevo del repo.

**Objetivo**: recibe un valor por el borde oeste externo, le suma una constante, lo
guarda en el scratchpad de la celda de memoria, y lo vuelve a emitir por el mismo borde
oeste externo — un patrón de "computar, resguardar en un buffer, y drenar" (útil, por
ejemplo, cuando el productor y el consumidor de un dato no están listos al mismo tiempo).

**Layout** (`ROWS=1, COLS=3`):

```
P00 = PE_Scalar   P01 = Routing_Cell   P02 = PE_Memory
```

**Programa de `P00` (PE_Scalar)** — 3 slots activos, 1 slot de relleno:

```cpp
MiAppInstr_C mov10;   mov10.opcode = OP_MOV; mov10.src_a = SRC_IMM; mov10.imm = 10;
                      mov10.dst = DST_REG; mov10.reg_dst = 0;             // reg0 = 10
MiAppInstr_C add_w;   add_w.opcode = OP_ADD; add_w.src_a = SRC_REG; add_w.reg_a = 0;
                      add_w.src_b = SRC_WEST; add_w.dst = DST_EAST;       // out_E = reg0 + in_W
MiAppInstr_C relay_e; relay_e.opcode = OP_MOV; relay_e.src_a = SRC_EAST;
                      relay_e.dst = DST_WEST;                            // out_W = in_E (eco del dato que vuelve)
program(0, 0, 0, mov10);
program(0, 0, 1, add_w);
program(0, 0, 2, relay_e);
// slot 3: sin programar -> NOP por defecto
```

**Programa de `P01` (Routing_Cell)** — un único contexto, paso bidireccional:

```cpp
program(0, 1, 0, make_routing_config_instr_c<MIAPP_DATA_W>(RC_NONE, RC_NONE, RC_FROM_W, RC_FROM_E));
```

**Secuencia de ejecución**:

```cpp
step(/*rst=*/true, ...);                 // realinear PC de todas las celdas a 0

in_W[0] = 5;                             // estimulo externo, se mantiene fijo
for (int i = 0; i < 6; i++) step(false, ...);   // dejar que Scalar+Routing se estabilicen

// Memoria (0,2), contexto 0: capturar el valor que llega por su puerto oeste
program(0, 2, 0, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_DST_ADDR, 0));
program(0, 2, 0, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_COUNT, 1));
program(0, 2, 0, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
program(0, 2, 0, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_DIR, 1));   // NoC(W)->SRAM
program(0, 2, 0, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_START, 0));
for (int i = 0; i < 3; i++) step(false, ...);   // dejar que la rafaga de 1 palabra corra y cierre

// Reprogramar memoria, contexto 1: drenar SRAM[0] de vuelta hacia el oeste
program(0, 2, 1, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_SRC_ADDR, 0));
program(0, 2, 1, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_COUNT, 1));
program(0, 2, 1, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_MODE, AccessController::MODE_DIRECT));
program(0, 2, 1, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_DIR, 0));   // SRAM->NoC(W)
program(0, 2, 1, make_memory_field_instr_c<MIAPP_DATA_W>(MEM_FIELD_START, 1));
for (int i = 0; i < 6; i++) step(false, ...);   // dejar que el dato vuelva: Memoria->Routing->Scalar->out_W

// out_W[0] deberia terminar en 15 (5 + 10)
```

Notá los 3 conteos "generosos" de ciclos (6, 3, 6) en vez de un cálculo exacto — es
exactamente la disciplina de la sección 2, Paso 5: cuando la ruta mezcla PEs y DMA, corré
de más y verificá, no calcules al límite.

## 4. Errores comunes (checklist antes de dar por listo un componente nuevo)

Lecciones ya pagadas por este proyecto — evitalas de entrada:

- [ ] **`csim_design` pasando no prueba que sintetiza.** El frontend de síntesis
  (`csynth_design`) es un parser distinto y más estricto que corre `g++` en `csim_design`.
  Corré siempre `csynth_design` real antes de confiar en un componente nuevo.
- [ ] **No uses contenedores de la STL** (`std::tuple`, `std::vector`, `std::map`, ...) en
  código que va a sintetizarse. `csynth_design` de Vitis HLS 2024.1 rechazó `std::tuple`
  de plano (y transitivamente los internos de `<array>`/`<string>` de esa libstdc++) aun
  cuando `csim_design` lo aceptaba sin quejarse. Si necesitás almacenamiento heterogéneo,
  seguí el patrón de `CellChain` en `CGRA_Mesh_Static_C.h` (cadena de herencia recursiva,
  sin STL).
- [ ] **Incluí `<cstdint>` explícitamente** si usás `uint32_t`/`int32_t` — `g++` a veces
  lo resuelve por una inclusión transitiva de otro header, pero el frontend de síntesis de
  Vitis HLS no siempre hace la misma resolución.
- [ ] **El top nunca puede quedar `inline`.** Necesita declaración en un `.h` y definición
  real en un `.cpp` separado — si el `.cpp` del diseño solo hace `#include` de un header
  con el cuerpo completo, `csynth_design` falla con "Top function not found".
- [ ] **Si tu malla es persistente (`static Mesh mesh;`) y usás `cgra_run<...>` o un patrón
  similar**, aplicá `rst=true` desde el **primer** ciclo de cada corrida nueva, no un
  ciclo después — de lo contrario un PC "viejo" que dejó la corrida anterior ejecuta una
  instrucción real contra bordes en cero antes de que el reset surta efecto (puede
  contaminar un acumulador justo después de limpiarlo).
- [ ] **Nunca asumas que un dato está listo el mismo ciclo en que lo presentaste** — toda
  celda ve las salidas de sus vecinos tal como quedaron el ciclo anterior. Antes de leer
  un resultado o disparar el siguiente paso de tu secuencia, corré suficientes `step()`
  para que se propague por cada salto de la cadena (ver sección 3, y la nota de "priming"
  en el paso 5 de la sección 2).
- [ ] **`INSTR_MEM_SIZE` como parámetro de template no siempre es deducible.** Si escribís
  un template genérico que recibe `CellTs...` como parameter pack, cualquier constante que
  necesites *fuera* del tipo concreto de las celdas (como "cuántos ciclos dura una fase")
  tiene que seguir siendo un parámetro de template explícito — no se puede inferir del
  parameter pack.
- [ ] **Si generalizás el storage de la malla o cambiás su forma de indexado**, revisá que
  no quede código asumiendo indexado `mesh.pe[r][c]` en runtime — con almacenamiento
  heterogéneo (`CellChain`) eso ya no compila; usá `mesh_read_outputs()` para volcar a un
  arreglo homogéneo primero si necesitás indexar por fila/columna en tiempo de ejecución.

## 5. Para seguir

- `project.md` — arquitectura completa, ambos tiers, resultados de síntesis reales.
- `gemm_hls_c/` + `Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c/` — referencia completa del
  camino "fases fijas" (`cgra_run<...>`).
- `cgra_hetero_2x2_demo_c/` + `Proyecto_HLS/hls_vitis_cgra_2x2_heterogeneous_c/` —
  referencia completa del camino "control crudo", y la única malla con los 4 tipos no
  triviales de celda ya combinados en un mismo diseño.
- Cabeceras de cada celda (`pe_hls_c/*/`, `memory_hls_c/`) — cada una documenta en su
  comentario de cabecera las decisiones no obvias específicas de ese tipo.
