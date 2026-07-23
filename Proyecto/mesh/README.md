# CGRA Mesh

Fabric estructural de la CGRA: `CGRA_Mesh_Heterogeneous<ROWS,COLS,DATA_W,VLEN>`
instancia un grid de Processing Elements (PE) y cablea sus puertos de vecino
(`in_N/S/E/W`/`out_N/S/E/W`) con `sc_signal`. Puro wiring — no tiene procesos propios ni
lógica de PE; las PEs mismas (`PE_scalar`/`PE_vector`/`PE_MAC`) viven en `../pe/`.

## Requisitos
- SystemC (variable de entorno `SYSTEMC_HOME` apuntando a la instalación, si no está en
  una ruta estándar del sistema).
- CMake >= 3.16 y un compilador con soporte C++17.
- (Opcional) [GTKWave](https://gtkwave.sourceforge.net/) para ver el waveform generado.

## Compilar
Desde la raíz del repo (el build está unificado, ver `CMakeLists.txt` raíz):
```
mkdir -p build && cd build
cmake ..
make
```
Los binarios de esta carpeta quedan en `build/mesh/`.

## Ejecutar
```
./CGRA_Mesh_1x1_Test__TB     # malla 1x1, una sola PE_vector con una instruccion de suma
./CGRA_Mesh_SmokeTest__TB    # malla 3x1, un PE de cada tipo
./CGRA_Mesh_ComplexTest__TB  # malla 3x3, 3 pipelines de 3 etapas (uno por tipo de PE)
```
Los tres generan un `.vcd` con el estado interno de la malla, visible con `gtkwave`.

## Definir un layout

Cada celda del grid puede ser un `PE_scalar`, `PE_vector` o `PE_MAC`. Qué tipo va en
cada celda lo decide el parámetro `layout` del constructor:

```cpp
CGRA_Mesh_Heterogeneous(sc_module_name name, const std::vector<CellKind>& layout);
```

`CellKind` (definido en `../pe/PE_Base.h`) es un enum con tres valores: `SCALAR`,
`VECTOR`, `MAC`. `layout` es un vector **plano**, no una matriz 2D — la celda de fila
`r` y columna `c` va en el índice `r*COLS + c` (row-major). Su tamaño debe ser
exactamente `ROWS*COLS`; si no coincide, el constructor aborta con `SC_REPORT_FATAL`.

Los dos tests de esta carpeta usan el patrón "una fila = un tipo de PE", pero el
mecanismo no tiene esa limitación — cualquier mezcla celda a celda es válida. Ejemplo,
una malla 2x2 con una celda de cada tipo más una escalar de relleno:

```cpp
static const int ROWS = 2, COLS = 2;
std::vector<CellKind> layout = {
    CellKind::SCALAR, CellKind::VECTOR,   // fila 0: (0,0) (0,1)
    CellKind::MAC,    CellKind::SCALAR,   // fila 1: (1,0) (1,1)
};
CGRA_Mesh_Heterogeneous<ROWS, COLS> mesh("mesh", layout);
```

El wiring de vecinos entre celdas es completamente independiente del tipo concreto: el
wire común de toda la malla es `PE_VectorData<DATA_W,VLEN>` (el escalar es un caso
degenerado del vector), así que celdas de distinto tipo se conectan entre sí sin
adaptación adicional de tu parte.

## Programar las PEs (cargar instrucciones)

La malla no expone un puerto de instrucciones — se programa por método directo:

```cpp
void load_instr(int row, int col, sc_uint<8> addr, const PE_Instruction<DATA_W>& instr);
void clear_instr(int row, int col);
```

`load_instr` escribe la instrucción en la celda `(row,col)`; hace falta **un flanco de
`clk`** para que quede latcheada dentro de esa PE. `clear_instr` deja de forzarla
(equivalente a no cargar nada). Mismo comportamiento sin importar el tipo de celda.

Una `PE_Instruction<DATA_W>` (definida en `../pe/pe_isa.h`) tiene estos campos:

| Campo | Significado |
|---|---|
| `opcode` | `OP_ADD`/`SUB`/`AND`/`OR`/`XOR`/`MOV`/`SLL`/`SRL`/`SRA`/`SLT`/`SLTU`/`MUL`/`MAC`/`NOP` |
| `src_a`, `src_b` | Origen de cada operando (`PE_Src`): `SRC_REG`, `SRC_NORTH/SOUTH/EAST/WEST` (vecino de malla), `SRC_IMM` (inmediato), `SRC_ACC` (solo `PE_MAC`) |
| `dst` | Destino del resultado (`PE_Dst`): `DST_REG`, `DST_NORTH/SOUTH/EAST/WEST`, `DST_ALL` (difunde a los 4 vecinos), `DST_ACC` (solo `PE_MAC`) |
| `reg_a`, `reg_b`, `reg_dst` | Índices de registro cuando `src`/`dst` es `REG` |
| `imm` | Inmediato usado cuando `src_a`/`src_b` es `SRC_IMM` |

`addr` (el segundo argumento de `load_instr`) es la posición dentro de la memoria de
instrucciones de esa celda (`INSTR_MEM_SIZE` en el template de la malla, default 16).
Los tres tests de esta carpeta instancian la malla con `INSTR_MEM_SIZE=1` — un programa
de una sola instrucción por celda — por eso siempre usan `addr=0`; con un
`INSTR_MEM_SIZE` mayor podés cargar varias instrucciones por celda en distintas `addr`.

Patrón de margen usado en los tres tests: cargar todas las instrucciones → `sc_start` de 1
ciclo de reloj → `clear_instr` de todas. Un caso especial: reprogramar una celda
`PE_MAC` **después de que ya está corriendo** (no en la carga inicial) necesita ~3
ciclos de margen en vez de 1, porque `PE_MAC_Cell` tiene un puente asíncrono adicional
(`bridge_instr_in()`) entre `load_instr` y la memoria de instrucciones real de esa PE.

## Calcular los valores "esperado" de los tests

Los pipelines de 3 etapas de `CGRA_Mesh_ComplexTest__TB.cpp` (`SUB`/`OR`/`XOR`
encadenados, con operandos negativos) son fáciles de calcular mal a mano — un bit
mal en un `AND`/`XOR` no se nota a simple vista. `tools/expected_values.py` es la
fuente de verdad: reproduce, sección por sección, los mismos literales que hoy están
escritos en los dos testbenches, con la misma semántica de 32 bits con signo que
`sc_int<32>` (complemento a 2, wraparound silencioso). Se corre así:
```
python3 mesh/tools/expected_values.py
```
Si se agrega un caso nuevo a cualquiera de los dos tests, agregar primero la llamada
correspondiente en el script y correrlo para obtener el literal — no calcular el
resultado de una cadena de opcodes a mano y confiar en que está bien.
