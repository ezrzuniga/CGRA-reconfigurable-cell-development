# CGRA Heterogénea Reconfigurable — Documentación General del Proyecto

Este documento describe qué es este proyecto, cómo está construido, cómo se programa el
hardware que modela, y cómo se ejecuta un algoritmo real sobre él. Está pensado para
alguien que revisa el repositorio por primera vez y quiere tanto entender la arquitectura
como poder usarla (compilarla, simularla, sintetizarla).

## 1. Qué es este proyecto

Es el modelo de una **CGRA** (Coarse-Grained Reconfigurable Array): un arreglo 2D de
celdas de cómputo/enrutamiento/memoria interconectadas en malla (Norte/Sur/Este/Oeste),
donde cada celda ejecuta su propio programa corto y se comunica solo con sus 4 vecinos
inmediatos. A diferencia de una FPGA (reconfigurable a nivel de compuerta) o un ASIC (fijo),
una CGRA reconfigura **la interconexión y el programa de cada celda** manteniendo el
datapath de cada celda fijo en silicio — el punto intermedio entre flexibilidad y
eficiencia energética/de área.

El proyecto existe en **dos representaciones paralelas del mismo diseño**:

- **SystemC** (`pe/`, `mesh/`, `memory/`, `mesh_wrapper/`, `riscv_dma_main_mem_components/`,
  y sus equivalentes completos dentro de `Proyecto/`): el modelo de referencia, con
  simulación ciclo-preciso, TLM-2.0 para el sistema completo (RISC-V + DMA + memoria
  principal), y el mayor detalle arquitectónico (incluye el enlace a un sistema RISC-V
  anfitrión). **No sintetizable** con las herramientas usadas en este proyecto (ver
  sección 3.1).
- **C/C++ puro + pragmas de Vitis HLS** (`Proyecto/pe_hls_c/`, `mesh_hls_c/`, `cgra_hls_c/`,
  `gemm_hls_c/`, `memory_hls_c/`, `cgra_hetero_2x2_demo_c/`): la migración del mismo
  datapath a una forma que Vitis HLS 2024.1 sí acepta como entrada de síntesis. Es la
  representación que efectivamente compila a IP de hardware (RTL), y la que se documenta
  con más detalle de "cómo usarlo" en este documento.

Ambos tiers modelan el **mismo ISA**, el **mismo contrato de puertos de malla**, y las
**mismas 5 variantes de celda** (PE escalar, PE vectorial, PE de multiply-accumulate,
celda de enrutamiento, celda de memoria). El tier C/HLS es el que se sintetiza; el tier
SystemC es el que sirve de referencia de comportamiento y el que eventualmente se
integra a un sistema RISC-V completo vía TLM.

## 2. Arquitectura de hardware

### 2.1 La malla y sus celdas

Una malla `ROWS x COLS` instancia una celda por posición. Cada celda expone exactamente
4 puertos de dato hacia sus vecinos (`in_N/in_S/in_E/in_W`, `out_N/out_S/out_E/out_W`) más
`clk/rst/enable` y un canal de carga de programa. Los bordes de la malla (fila 0, fila
`ROWS-1`, columna 0, columna `COLS-1`) son los únicos puntos donde `in_*`/`out_*` se
conectan al mundo exterior; el resto son enlaces internos entre celdas vecinas.

Las 5 variantes de celda, todas construidas sobre el mismo ISA (sección 2.3):

| Celda | Rol | Estado interno propio |
|---|---|---|
| **PE escalar** | ALU RV32I-like de 1 operando escalar por ciclo | banco de registros escalar, PC, memoria de instrucciones |
| **PE vectorial** | Igual ISA, SIMD de ancho fijo `VLEN` (lanes independientes, sin cruce entre ellos) | banco de registros vectorial, PC, memoria de instrucciones |
| **PE MAC** | Como la vectorial, más un acumulador direccionable (`acc += a*b` en 1 ciclo) | lo anterior + acumulador vectorial |
| **Celda de enrutamiento** | Switch-box puramente combinacional: crossbar de 4 puertos, banco de hasta 4 configuraciones (contextos) | banco de configuraciones + contexto activo |
| **Celda de memoria** | SRAM local + motor de ráfaga DMA (SRAM↔NoC, SRAM↔SRAM), 4 contextos de transferencia | SRAM + registros de contexto + FSM de ráfaga |

Una malla real (p. ej. GEMM 2x2, sección 5) mezcla estos tipos según un **layout fijo en
tiempo de compilación** — no hay polimorfismo en tiempo de ejecución en el tier C/HLS
(ver sección 2.4).

### 2.2 ISA compartido

Un único juego de instrucciones (`pe_isa.h` en SystemC, `pe_isa_hls_c.h` en C/HLS) sirve a
las 3 variantes de PE:

- **Opcodes** (`PE_Opcode`): aritmética/lógica estilo RV32I completa —
  `ADD/SUB/AND/OR/XOR/MOV/SLL/SRL/SRA/SLT/SLTU/MUL` — más `MAC` (solo la PE MAC lo trata
  de forma especial).
- **Fuentes de operando** (`PE_Src`): `SRC_REG` (banco propio), `SRC_NORTH/SOUTH/EAST/WEST`
  (leer un vecino de malla), `SRC_IMM` (inmediato de la instrucción), `SRC_ACC` (solo PE
  MAC, lectura no destructiva del acumulador).
- **Destinos** (`PE_Dst`): `DST_REG`, `DST_NORTH/SOUTH/EAST/WEST` (escribir hacia un
  vecino), `DST_ALL` (broadcast a los 4 vecinos a la vez), `DST_ACC` (solo PE MAC).
- **Formato de instrucción** (`PE_Instruction<DATA_W>`): `opcode` (4 bits), `src_a`/`src_b`
  (3 bits cada uno), `dst` (3 bits), `reg_a`/`reg_b`/`reg_dst` (5 bits cada uno, índice de
  registro), `imm` (`DATA_W` bits con signo).
- **Dato de malla** (`PE_VectorData<DATA_W,VLEN>`, alias `Link`): `VLEN` lanes
  independientes de `DATA_W` bits — el "wire" único de toda la malla. Una PE escalar
  (`VLEN` lógico = 1 en su propio datapath) sigue hablando este mismo tipo hacia afuera:
  hace *broadcast* de su resultado escalar a los `VLEN` lanes del wire al escribir, y lee
  solo la lane 0 al leer un vecino — el escalar es un caso degenerado del vector, nunca al
  revés.

Cada ciclo habilitado (`enable=true`), una PE tipo escalar/vectorial/MAC hace
`fetch (instr_mem[pc]) -> select_src(a,b) -> ALU -> writeback (si dst) -> pc++`. Un
`rst` realinea solo el PC a 0 — **no** borra `reg_file`/`instr_mem`/`acc`: el programa
cargado y el estado de cómputo sobreviven a un reset, solo se reinicia la secuencia de
ejecución (relevante para la reprogramabilidad, sección 4).

### 2.3 Celda de enrutamiento

Un mux puramente combinacional por salida: cada una de `out_N/S/E/W` puede tomar el valor
de cualquiera de las 4 entradas (`in_N/S/E/W`) o quedar en 0 (`RC_NONE`), según un banco de
hasta 4 configuraciones (`config_bank[0..3]`) y un contexto activo (`active_ctx`,
persistente). No tiene ALU ni memoria de instrucciones — su "programa" es puramente de
enrutamiento. Sirve para desviar tráfico entre celdas no adyacentes en el sentido lógico
del algoritmo (p. ej. conectar una celda de memoria con una PE que no es su vecina
física-lógica directa en el flujo de datos).

### 2.4 Celda de memoria

SRAM local (arreglo direccionado por palabra) + un motor de DMA de 1 palabra por ciclo,
con hasta 4 "contextos" de transferencia preconfigurables (dirección origen, dirección
destino, stride, cantidad de palabras, modo `DIRECT`/`STRIDE`, dirección de transferencia:
`SRAM->NoC`, `NoC->SRAM`, o `SRAM->SRAM`). El puerto de red-en-chip (NoC) está simplificado
a **un solo borde** (oeste: `in_W`/`out_W`); los otros 3 bordes están cableados por
uniformidad de grilla pero siempre en cero. Dos `AccessController` (uno para origen, uno
para destino — ver `Proyecto/memory/Access_controller.h`, reusado sin cambios en ambos
tiers) generan la secuencia de direcciones de cada ráfaga.

### 2.5 Wire unificado y disciplina de temporización

Toda la malla —sin importar qué mezcla de celdas tenga— habla el mismo tipo de dato
(`Link = PE_VectorData<DATA_W,VLEN>`) en sus 4 puertos de borde. Esto es lo que permite
construir una malla *heterogénea*: cualquier celda puede ir en cualquier posición porque
todas cumplen el mismo contrato de E/S, sin importar qué tan distinto sea su
comportamiento interno.

Disciplina de registro (idéntica en ambos tiers, y no obvia si se lee el código de forma
apurada): en cada ciclo, **todas** las celdas leen las salidas de sus vecinos tal como
quedaron al **final del ciclo anterior** — nunca el valor que un vecino está calculando
"ahora mismo" en el mismo ciclo. Esto evita lazos combinacionales entre celdas de la
malla y hace que el comportamiento sea análogo a un registro de salida por celda. En la
implementación C/HLS esto se modela explícitamente tomando una "foto" (`snapshot`) de las
salidas viejas de las `ROWS*COLS` celdas antes de avanzar a ninguna en el ciclo actual
(`mesh_step()`, ver `CGRA_Mesh_Static_C.h`); en SystemC lo da naturalmente la semántica de
`sc_signal`/procesos sensibles a flanco.

## 3. Dos tiers: SystemC vs. C/HLS

### 3.1 Por qué existen dos tiers

Vitis HLS 2024.1 **rechaza SystemC como entrada de síntesis** de forma categórica:

```
ERROR: [HLS 200-637] SystemC input is not supported
SystemC is not supported!
```

Esto no es un problema de flags o pragmas — el flujo unificado de Vitis HLS discontinuó
la síntesis de SystemC que sí soportaba Vivado HLS clásico. `csim_design` (simulación C,
que corre con g++ + libSystemC real) sí pasa sin problema sobre el diseño SystemC, lo cual
enmascaró el problema hasta que se corrió `csynth_design` (el frontend de síntesis real)
contra una instalación real de la herramienta.

La solución fue migrar el mismo datapath (misma ISA, mismo wiring N/S/E/W, misma FSM) a
C/C++ puro con tipos de ancho fijo de Vitis (`ap_int`/`ap_uint`) y pragmas HLS, sin
`sc_module`/`sc_signal`/puertos SystemC de ningún tipo. El tier SystemC se conserva
íntegro como referencia histórica y de comportamiento — no se borró nada.

Una segunda limitación del frontend de síntesis, descubierta más adelante y menos
conocida: **también rechaza ciertos tipos de la STL** (`std::tuple`, y transitivamente los
internos de `<array>`/`<string>` de esa libstdc++), con el mismo patrón — `csim_design`
los acepta, `csynth_design` no (`ERROR: [HLS 207-2916] C++ requires a type specifier for
all declarations`, apuntando a `bits/stl_pair.h`/`bits/basic_string.h`/`array`/`tuple`).
Esto obligó a reemplazar el almacenamiento heterogéneo de la malla (que originalmente usó
`std::tuple<CellTs...>`) por una cadena de herencia recursiva escrita a mano
(`CellChain`, sección 4). La lección operativa que deja esto para cualquier trabajo futuro
sobre el tier C/HLS: **`csim_design` pasando no es garantía de que sintetice** — el único
chequeo confiable es correr `csynth_design` (y, para máxima confianza, `cosim_design`) con
una instalación real de Vitis HLS.

### 3.2 Mapa de carpetas — tier C/HLS (el que se sintetiza)

```
Proyecto/
├── pe_hls_c/
│   ├── pe_isa_hls_c.h        ISA compartido (ap_int/ap_uint, sin SystemC)
│   ├── mac/PE_MAC_HLS_C.h    PE MAC: datapath + acumulador
│   ├── scalar/PE_Scalar_HLS_C.h   PE escalar: datapath escalar + puente lane0/broadcast
│   ├── vector/PE_Vector_HLS_C.h  PE vectorial: igual a MAC sin acumulador
│   └── routing/Routing_Cell_HLS_C.h  Switch-box de 4 contextos
├── memory_hls_c/PE_Memory_HLS_C.h   SRAM + motor de ráfaga DMA
├── mesh_hls_c/CGRA_Mesh_Static_C.h  Malla heterogénea genérica (CellTs... variádico)
├── cgra_hls_c/CGRA_Top_C.h          Template genérico de "top reprogramable" (cgra_run<...>)
├── gemm_hls_c/                     Aplicación concreta: GEMM 2x2 sobre el template
└── cgra_hetero_2x2_demo_c/          Demo: malla heterogénea 2x2 cruda (sin FSM de aplicación)
```

Cada header de celda expone exactamente 3 funciones libres con la misma forma de firma en
las 5 celdas — el mecanismo que hace posible la heterogeneidad sin `virtual` ni una clase
de trait escrita a mano (ver sección 4.1 para el detalle):

```cpp
void <tipo>_step(State&, bool rst, bool enable, Link in_N, Link in_S, Link in_E, Link in_W);
void <tipo>_program(State&, ap_uint<8> slot, const PE_Instruction<DATA_W>& instr);
void <tipo>_clear_acc(State&);   // no-op salvo en PE_MAC, la única con acumulador
```

más un overload de nombre genérico compartido por las 5 (`cell_step`/`cell_program`/
`cell_clear_acc`) que solo reenvía a la función ya nombrada — la malla llama siempre al
nombre genérico, y como conoce el tipo concreto de cada celda en tiempo de compilación
(parameter pack `CellTs...`), la resolución de sobrecarga de C++ elige la función correcta
sin costo de runtime.

## 4. Programación del hardware

### 4.1 Dos canales separados: programar vs. correr

Cada celda separa con claridad **cargar configuración** de **avanzar un ciclo de reloj**:

- **`<tipo>_program(state, slot, instr)`** — un "poke" directo a la memoria de
  instrucciones (PEs) o al banco de configuraciones (`slot` = índice de contexto, para
  enrutamiento/memoria). No consume un ciclo de `mesh_step()`, no compite con la
  ejecución. `slot` es deliberadamente polimórfico: dirección de `instr_mem` para las 3
  variantes de PE, índice de contexto (0..3) para enrutamiento y memoria — mismo campo,
  distinto significado documentado por tipo.
- **`<tipo>_step(state, rst, enable, in_N, in_S, in_E, in_W)`** — un ciclo de reloj real:
  fetch/ejecución/escritura de salida para las PEs; avance de ráfaga para memoria; mux
  combinacional para enrutamiento.

A nivel de malla completa, esto se expone como:

```cpp
mesh_program(mesh, pe_row, pe_col, slot, instr);   // escribe una celda, no consume ciclo
mesh_step(mesh, rst, enable, bound_in_N, bound_in_S, bound_in_W, bound_in_E);  // 1 ciclo
mesh_read_outputs(mesh, out_N, out_S, out_E, out_W);  // vuelca los 4 bordes de salida
```

`mesh_program` recibe fila/columna como valores de **runtime** (el host puede programar
cualquier celda), pero el almacenamiento heterogéneo (`CellChain<I, CellTs...>`, una
cadena de herencia recursiva sin STL — ver sección 3.1 sobre por qué no es
`std::tuple`) exige un índice de **compilación**. Esto se resuelve desenrollando en
compilación las `ROWS*COLS` posiciones (fold expression sobre un `index_seq` hecho a mano)
y comparando en runtime, en cada posición desenrollada, si su `(r,c)` constexpr coincide
con el `(pe_row,pe_col)` pedido — exactamente una posición ejecuta la escritura, como un
mux/case en hardware.

### 4.2 Programar la celda de enrutamiento

`make_routing_config_instr_c<DATA_W>(sel_N, sel_S, sel_E, sel_W)` empaqueta 4 selectores
de 4 bits (`RC_NONE/RC_FROM_N/RC_FROM_S/RC_FROM_E/RC_FROM_W`, uno por salida) en los 4
nibbles altos de `instr.imm`. `routing_cell_program(state, ctx, instr)` decodifica esos 4
selectores en `config_bank[ctx]` **y además activa ese contexto de inmediato**
(`active_ctx = ctx`) — programar un contexto también conmuta el crossbar a él en la misma
llamada; no hace falta un puerto de selección de contexto aparte por ciclo.

### 4.3 Programar la celda de memoria

`make_memory_field_instr_c<DATA_W>(field, value)` construye una instrucción donde
`instr.reg_dst` selecciona el campo (`MEM_FIELD_SRC_ADDR/DST_ADDR/STRIDE/COUNT/MODE/DIR`) e
`instr.imm` lleva el valor. `memory_program(state, ctx, instr)` escribe campos de a uno por
llamada sobre el contexto `ctx`; escribir el campo especial `MEM_FIELD_START` dispara la
ráfaga: configura los 2 `AccessController` con los valores ya acumulados en ese contexto y
pasa la FSM a `MEM_ST_BURST`. Desde ahí, cada `memory_step()` transfiere una palabra
(dirección determinada por `dir`: 0 = SRAM→NoC(oeste), 1 = NoC(oeste)→SRAM, 2 = SRAM→SRAM)
hasta que los controladores de acceso se agotan, momento en que la FSM vuelve a `IDLE` y
señaliza `done=true`.

### 4.4 Persistencia entre corridas — el punto que hace esto "reconfigurable" de verdad

En los tops de aplicación (`GEMM_2x2_HLS_Top_C`, `CGRA_Hetero_2x2_Demo_Top_C`), la malla
vive en una variable `static` dentro del `.cpp` — el único estado con memoria de todo el
diseño. Esto separa completamente "subir un programa" de "ejecutarlo": el host puede
programar la malla una sola vez y disparar la ejecución muchas veces con operandos
distintos, sin volver a programar — el comportamiento esperado de hardware reconfigurable
real (firmware cargado una vez, corrido muchas veces), no un programa fijo grabado en el
hardware en tiempo de síntesis.

## 5. Ejecución de un algoritmo — caso de estudio: GEMM 2x2

### 5.1 El template genérico `cgra_run<...>`

`Proyecto/cgra_hls_c/CGRA_Top_C.h` define un template reutilizable,
`cgra_run<ROWS,COLS,DATA_W,VLEN,INSTR_MEM_SIZE,NUM_PHASES,CellTs...>`, del cual "se saca"
cualquier CGRA sintetizable concreta con un wrapper de ~25 líneas (constantes de tamaño +
un `static Mesh mesh;` + reenviar puertos). Expone 2 caminos de control mutuamente
excluyentes por llamada:

1. `prog_valid=true`: escribe una instrucción vía `mesh_program()`, `done=true` de
   inmediato.
2. `start=true`: corre las `NUM_PHASES` fases completas en una sola invocación, usando el
   programa ya residente en `instr_mem` (subido antes con el camino 1). Por fase se
   presenta un nuevo snapshot de los 4 bordes de la malla y se corren `INSTR_MEM_SIZE`
   ciclos de `mesh_step` — una fase = una pasada completa por la memoria de instrucciones
   de cada PE.

La FSM interna (`ST_REALIGN -> ST_PHASE_RUN -> ST_WAIT_DONE -> ST_DONE`) usa un registro
explícito "pegajoso" (`MeshDrive`, con miembros `curr`/`nxt`) para preservar la disciplina
de que "lo que se presenta a la malla este ciclo, la malla lo ve recién el ciclo
siguiente" — el mismo patrón de retardo de registro de la sección 2.5, aplicado ahora a
las entradas de la FSM hacia la malla, no solo entre celdas.

Detalle no obvio corregido durante la validación: como la malla es persistente entre
corridas (`static`), el primer `mesh_step()` de cada invocación debe aplicar `rst=true`
**desde ya**, no un ciclo después como hacía el diseño original de un solo uso — de lo
contrario un PC "viejo" de la corrida anterior ejecutaría, con `instr_mem` ya cargada, una
instrucción real de ese PC contra bordes en cero antes de que el reset surta efecto,
contaminando un acumulador justo después de limpiarlo.

### 5.2 El algoritmo: multiplicación de matrices 2x2

`Proyecto/gemm_hls_c/GEMM_2x2_Mesh_C.h` instancia `CGRA_Mesh_Static_C<2,2,32,1,` 4 celdas
`PE_MAC>` (el caso homogéneo es simplemente pasar el mismo tipo 4 veces en el parameter
pack heterogéneo) y define el **programa espacial**: 4 slots de instrucción por celda,
repetidos una vez por fase (`k=0,1`, el índice de suma del producto matricial):

```
        slot0            slot1            slot2            slot3
P00  MOV W->E(a)     MOV N->S(b)      MAC(W,N)->ACC    MOV ACC->W
P01  MOV N->S(b)     MAC(W,N)->ACC    NOP              MOV ACC->E
P10  MOV W->E(a)     NOP              MAC(W,N)->ACC    MOV ACC->W
P11  NOP             MAC(W,N)->ACC    NOP              MOV ACC->E
```

`A` entra por `in_W[fila]`, `B` por `in_N[columna]`; cada celda `P[i][j]` acumula
`C[i][j] += A[i][k]*B[k][j]` en su propio acumulador, relevando de paso los operandos que
la celda vecina necesita (`MOV W->E`, `MOV N->S`) para que el dato de `A`/`B` fluya hacia
la derecha/abajo por la malla en el mismo ciclo. `C` sale por `out_W[fila]` (columna 0) y
`out_E[fila]` (columna 1).

### 5.3 Secuencia completa de uso (lo que hace el testbench de referencia)

1. Subir las 16 instrucciones del programa espacial (4 celdas × 4 slots) con 16 llamadas
   `prog_valid=true`.
2. Presentar los operandos de fase 0 y fase 1 de `A`/`B` en `in_W`/`in_N`, disparar
   `start=true` — una sola invocación corre las 2 fases completas y entrega `C` en
   `out_W`/`out_E`.
3. Repetir el paso 2 con operandos distintos **sin repetir el paso 1** — demuestra que el
   programa sobrevive entre corridas (sección 4.4). El testbench de referencia
   (`GEMM_2x2_HLS_Top_C__TB.cpp`) corre 2 casos de prueba (enteros positivos, y con
   negativos) exactamente así.

## 6. La demo heterogénea 2x2

`Proyecto/cgra_hetero_2x2_demo_c/` (+ el proyecto Vitis HLS
`Proyecto_HLS/hls_vitis_cgra_2x2_heterogeneous_c/`) prueba que la malla heterogénea
sintetiza de verdad con una mezcla real de tipos de celda, no solo con el mismo tipo
repetido (como GEMM). Layout fijo:

```
P00 = Routing_Cell    P01 = PE_Memory
P10 = PE_Scalar       P11 = PE_Vector
```

A diferencia de GEMM, este top es deliberadamente **crudo** — no hay todavía una
aplicación específica con su propio horario de fases: expone solo `mesh_program` (programar
una celda) y un `mesh_step` (correr un ciclo), dejando que un host (software o testbench)
orqueste la secuencia que su aplicación necesite. El escenario validado por su testbench:

1. **Round-trip Routing↔Memoria**: un valor entra por el borde oeste externo (fila 0),
   `Routing_Cell` lo desvía hacia `PE_Memory` (DMA NoC→SRAM), se reprograma la memoria
   para el viaje de vuelta (DMA SRAM→NoC), y `Routing_Cell` lo saca de nuevo al borde
   oeste externo.
2. **Pipeline Scalar→Vector** (en paralelo, en la otra diagonal): un valor entra por el
   borde oeste externo (fila 1), `PE_Scalar` lo combina con una constante y lo manda al
   este, `PE_Vector` lo recibe, suma otra constante, y lo saca por el borde este externo.

## 7. Flujo de desarrollo y verificación

Todo componente nuevo del tier C/HLS se valida en 2 etapas, en este orden:

1. **g++ standalone** (rápido, sin licencia de Vitis, usando `ap_int.h` de una
   instalación local de Vitis HLS): confirma la lógica de cada celda/malla/top de forma
   aislada antes de involucrar la herramienta real. Cada celda y cada top tiene su propio
   `*__TB.cpp` con esta forma de prueba.
2. **Vitis HLS real** (`csim_design -> csynth_design -> cosim_design -> export_design`):
   la única validación que de verdad confirma sintetizabilidad (sección 3.1) — nunca dar
   por completo un componente del tier C/HLS sin haber corrido al menos `csynth_design`
   contra la herramienta real.

## 8. Resultados de síntesis (Vitis HLS 2024.1, `xck26-sfvc784-2LV-c`, reloj 4 ns / 250 MHz)

Ambos proyectos completaron `csim_design` → `csynth_design` → `cosim_design` (PASS) →
`export_design` sin errores.

**GEMM 2x2** (`GEMM_2x2_HLS_Top_C`, malla homogénea de 4 `PE_MAC`):

| Recurso | Usado | Disponible | Utilización |
|---|---|---|---|
| BRAM_18K | 0 | 288 | 0% |
| DSP | 12 | 1248 | ~0% |
| FF | 7571 | 234240 | 3% |
| LUT | 7492 | 117120 | 6% |

Latencia estimada por invocación: variable (`cgra_run` corre un número de ciclos
dependiente de `NUM_PHASES`/`INSTR_MEM_SIZE`, con reloj estimado en 2.89 ns contra un
target de 4 ns).

**Demo heterogéneo 2x2** (`CGRA_Hetero_2x2_Demo_Top_C`, Routing+Memoria+Scalar+Vector):

| Recurso | Usado | Disponible | Utilización |
|---|---|---|---|
| BRAM_18K | 1 | 288 | ~0% |
| DSP | 8 | 1248 | ~0% |
| FF | 2880 | 234240 | 1% |
| LUT | 3578 | 117120 | 3% |

Latencia por invocación (un ciclo de `mesh_step` sobre las 4 celdas): 2–11 ciclos según la
celda más lenta involucrada (la celda de enrutamiento, puramente combinacional pero con
mayor profundidad lógica, domina el peor caso: hasta 8 ciclos estimados).

Ambos diseños entran cómodamente en el dispositivo objetivo (Kria KV26/K26 SoM,
`xck26-sfvc784-2LV-c`) con margen amplio para escalar el tamaño de la malla.

## 9. Cómo compilar y ejecutar

### 9.1 Tier SystemC (modelo de referencia, simulación ciclo-precisa)

Requiere SystemC (TLM-2.0) y CMake ≥ 3.16 con C++17. Desde `Proyecto/`:

```bash
export SYSTEMC_HOME=/ruta/a/tu/instalacion/systemc   # si no está en una ruta estándar
mkdir -p build && cd build
cmake .. && make -j4
ctest   # corre todos los testbenches registrados
```

Cada testbench es un binario independiente bajo `build/<carpeta>/` y genera su propio
`.vcd` inspeccionable con GTKWave. Ver `README.md` (raíz) para el listado completo de
binarios por carpeta.

### 9.2 Tier C/HLS (síntesis real)

Requiere Vitis HLS 2024.1 (o compatible). Cada subcarpeta de `Proyecto_HLS/` es un
proyecto independiente con su propio `run_hls.tcl`:

```bash
source /path/to/Xilinx/Vitis_HLS/2024.1/settings64.sh
cd Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c            # o hls_vitis_cgra_2x2_heterogeneous_c
vitis-run --mode hls --tcl run_hls.tcl
```

Ejecuta `csim_design -> csynth_design -> cosim_design -> export_design`. Reportes de
síntesis en `<proyecto>/solution1/syn/report/`, IP exportado en
`<proyecto>/solution1/impl/ip/`.

Antes de intentar el flujo real, cada celda/top individual puede validarse standalone con
g++ compilando su `*__TB.cpp` correspondiente contra `ap_int.h` de la instalación de
Vitis (sin necesidad de licencia ni de correr la herramienta completa).

### 9.3 Instanciar una CGRA propia

Cualquier aplicación nueva sobre el template genérico repite el patrón de
`gemm_hls_c/GEMM_2x2_HLS_Top_C.h`/`.cpp` (~25 líneas): declarar las constantes propias
(`ROWS/COLS/DATA_W/VLEN/INSTR_MEM_SIZE/NUM_PHASES`), el layout de celdas
(`CGRA_Mesh_Static_C<..., Tipo1, Tipo2, ...>`), un `static Mesh mesh;` dentro del `.cpp`, y
reenviar los puertos a `cgra_run<...>(mesh, ...)` — toda la FSM de fases, la interfaz de
programación y el wiring de bordes se reutilizan sin cambios. Para una aplicación sin FSM
de fases propia (como la demo heterogénea, sección 6), se puede exponer `mesh_program`/
`mesh_step` directamente sin pasar por `cgra_run`.

## 10. Limitaciones conocidas y trabajo futuro

- El tier C/HLS todavía no tiene una aplicación concreta que ejercite los 5 tipos de
  celda **con una FSM de fases propia** (GEMM solo usa PE_MAC; la demo heterogénea usa un
  top crudo sin horario de aplicación) — es el paso natural siguiente ahora que las 5
  celdas están portadas y la malla es genuinamente heterogénea.
- `PE_scalar`/`PE_vector`/`PE_MAC` no tienen señal de "terminé" — la malla no autodetecta
  el fin de un programa con PC en loop; un integrador externo (el `cgra_run`/FSM de
  aplicación, o eventualmente un sistema RISC-V) debe correr un número fijo de ciclos.
- La integración TLM-2.0 con un sistema RISC-V + DMA (`riscv_dma_main_mem_components/`)
  existe solo en el tier SystemC; el `cgra_socket` de `CSR_DMA` sigue sin implementar del
  lado CGRA. Portar esa integración al tier C/HLS (o conectar el tier C/HLS sintetizado
  detrás de un puente TLM↔señales planas) no se ha hecho todavía.
- El almacenamiento heterogéneo de la malla C/HLS (`CellChain`) exige que el layout de
  celdas se fije en tiempo de compilación (parameter pack `CellTs...`) — no existe (ni se
  ha buscado) una forma de cambiar qué tipo de celda ocupa una posición sin resintetizar;
  lo que sí es reconfigurable en runtime es el *programa* de cada celda (sección 4.4), no
  su *tipo*.

## 11. Índice de archivos clave

| Archivo | Qué contiene |
|---|---|
| `pe/CLAUDE.md`, `mesh/CLAUDE.md` | Detalle de diseño del tier SystemC simplificado (histórico, sin routing/memoria) |
| `Proyecto/pe/`, `Proyecto/mesh/`, `Proyecto/memory/` | Tier SystemC completo y actual (con routing + memoria integrados) |
| `Proyecto/pe_hls_c/pe_isa_hls_c.h` | ISA compartida del tier C/HLS |
| `Proyecto/pe_hls_c/{mac,scalar,vector,routing}/` | Las 4 celdas tipo PE/routing en C/HLS |
| `Proyecto/memory_hls_c/PE_Memory_HLS_C.h` | Celda de memoria en C/HLS |
| `Proyecto/mesh_hls_c/CGRA_Mesh_Static_C.h` | Malla heterogénea genérica (`CellTs...`, `CellChain`) |
| `Proyecto/cgra_hls_c/CGRA_Top_C.h` | Template genérico de top reprogramable (`cgra_run<...>`) |
| `Proyecto/gemm_hls_c/` | Aplicación GEMM 2x2 sobre el template |
| `Proyecto/cgra_hetero_2x2_demo_c/` | Demo de malla heterogénea cruda (Routing+Memoria+Scalar+Vector) |
| `Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c/README.md` | Instrucciones detalladas del proyecto Vitis HLS de GEMM |
| `Proyecto_HLS/hls_vitis_cgra_2x2_heterogeneous_c/README.md` | Instrucciones detalladas del proyecto Vitis HLS heterogéneo |
| `architecture/` | Diagramas `.svg` de arquitectura (mallas de prueba, bloques del reprogramable) |
| `CLAUDE.md` (raíz, no versionado) | Registro histórico de decisiones de diseño cross-cutting |
