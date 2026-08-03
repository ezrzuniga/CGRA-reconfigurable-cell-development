# hls_vitis_gemm_2x2_cgra

Vitis HLS project for `GEMM_2x2_HLS_Top`, la CGRA 2x2 de mapeo espacial para GEMM
(`../../Proyecto_SystemC/gemm_hls/GEMM_2x2_HLS_Top.h`) — sintetiza la malla real
(`CGRA_Mesh_Static<2,2,32,1,PE_MAC_Cell_HLS x4>`), no una reimplementación
aritmética aparte.

## Por qué esto es distinto de `hls_vitis_sum_reduction`

Ese otro proyecto reproduce solo el resultado aritmético de un test que corre sobre
TLM-2.0 puro (sockets, `tlm_generic_payload`, `SC_THREAD`+`wait()`) — código
estructuralmente no sintetizable bajo ninguna forma, así que no había arquitectura de
malla real que llevar a `csynth_design` ahí.

`mesh_hls`/`pe_hls` (rama `cgra_hls`) sí se construyeron para ser SystemC sintetizable
por HLS: sin TLM, sin `new`/virtuales, un solo `SC_METHOD` por PE, topología fija en
compile-time. Este proyecto pone esa malla real como *top* de Vitis HLS, detrás de un
wrapper (`GEMM_2x2_HLS_Top`) que:

- Mantiene el límite externo de puertos **escalar** (`sc_in<sc_int<32>>` por cada
  entrada/salida, sin `Link`/`PE_VectorData` ni `sc_vector` de puertos en el top) — el
  empaquetado a `Link` pasa a ser interno, en la conexión con la malla.
- Reemplaza `load_instr()`+`advance_cycles()` del testbench de simulación (C++, no
  sintetizable) por una FSM propia de un solo `SC_METHOD`: carga el programa espacial
  de 4 slots en las 4 PEs, limpia acumuladores, corre las 2 fases (`k=0,1`), señaliza
  `done`. El host solo necesita dejar `a00..b11` estables y pulsar `start`.

## 1) Requirements

### Software
- Vitis HLS 2024.1 (o una versión compatible con el flujo de Vivado)
- Entorno de shell Linux (Ubuntu recomendado)

### Environment setup
Antes de correr HLS, cargar el entorno de herramientas Xilinx, por ejemplo:

```bash
source /path/to/Xilinx/Vitis_HLS/2024.1/settings64.sh
```

## 2) How to run

Desde esta carpeta (`hls_vitis_gemm_2x2_cgra/`):

```bash
vitis_hls -f run_hls.tcl
```

Esto ejecuta el flujo completo definido en `run_hls.tcl`:
- C Simulation (`csim_design`) — corre `GEMM_2x2_HLS_Top__TB.cpp` (el mismo testbench
  que ya se compila y pasa con g++/SystemC normal en `Proyecto_SystemC/build`, referenciado
  aquí por ruta relativa, no duplicado).
- C Synthesis (`csynth_design`)
- C/RTL Co-simulation (`cosim_design -rtl verilog`)
- IP export (`export_design -format ip_catalog`)

## 3) Expected outputs

- Directorio del proyecto: `gemm_2x2_cgra_prj/`
- Reportes de síntesis: `gemm_2x2_cgra_prj/solution1/syn/report/`
- Artefactos de co-simulación: `gemm_2x2_cgra_prj/solution1/sim/`
- IP exportado: `gemm_2x2_cgra_prj/solution1/impl/ip/`

`GEMM_2x2_HLS_Top__TB.cpp` imprime PASS/FAIL por caso (2 casos: enteros positivos y con
negativos, 4 valores de `C` cada uno) y un resumen final, igual que el resto de los
testbenches de `Proyecto_SystemC/`.

## 4) Estado confirmado: bloqueado por Vitis HLS 2024.1 (SystemC no soportado)

Corriendo `vitis_hls -f run_hls.tcl` con una instalación real de Vitis HLS 2024.1:
`csim_design` pasa (corre con g++ + libSystemC real, no con el frontend de
síntesis), pero `csynth_design` falla con:

```
ERROR: [HLS 200-637] SystemC input is not supported
SystemC is not supported!
```

(ver `vitis_hls.log:35`). No es un problema de flags/pragmas ni de esta versión en
particular del diseño: el flujo unificado de Vitis HLS rechaza categóricamente
cualquier top escrito como `sc_module` — la síntesis SystemC que sí soportaba
Vivado HLS clásico fue discontinuada. No hay forma de destrabar esto sin migrar el
top (y su jerarquía) a C/C++ plano + pragmas HLS.

Esa migración ya se hizo, preservando la misma arquitectura (memoria de
instrucciones por PE, wiring N/S/E/W explícito, FSM de fases) sin SystemC —
ver `../hls_vitis_gemm_2x2_cgra_c/` (`Proyecto_C/pe_hls_c/`, `Proyecto_C/mesh_hls_c/`,
`Proyecto_C/gemm_hls_c/`). Ahí sí pasan `csim_design`/`csynth_design`/
`cosim_design`/`export_design` reales. Esta carpeta queda intacta como referencia
histórica de la variante SystemC-HLS (que sirvió para simular y validar la
arquitectura antes de saber que Vitis HLS 2024.1 no la sintetiza).

Riesgos que se habían anticipado sin poder correr `vitis_hls` (quedan como
referencia, ya no aplican para decidir si esta carpeta sirve — no sirve para
síntesis en ninguna versión del flujo unificado):

- **Structs con `std::array` como tipo de canal interno**: `Link = PE_VectorData<32,1>`
  envuelve `std::array<sc_int<32>,1>`. No es un puerto del *top* (evitado a propósito),
  pero sí es el tipo de las señales internas entre `GEMM_2x2_HLS_Top` y la malla, y
  entre celdas dentro de la malla. Si `csynth_design` se queja de este tipo, revisar
  `solution1/syn/report` por mensajes sobre `PE_VectorData`.
- **Herencia recursiva de `CellChain`** (`mesh_hls/CGRA_Mesh_Static.h`): el
  almacenamiento de las 4 PEs es una cadena de herencia (`CellChain<0,...>` hereda de
  `CellChain<1,...>`, etc.), no un arreglo plano. Si Vitis HLS aplana esto distinto a
  como lo hace g++, puede aparecer en el reporte de área con nombres de jerarquía
  inesperados — no debería afectar la funcionalidad, pero vale revisar.
- **Aviso de deprecación de `SC_HAS_PROCESS`**: ya aparece como warning (no error) al
  compilar con g++ 14 — es un macro marcado obsoleto por IEEE 1666-2023 pero sigue
  siendo el mecanismo que Vitis HLS 2024.1 espera; no debería bloquear la síntesis.
- **`GEMM_2x2_HLS_Top::tick()` tiene 10 estados con conteo de ciclos ajustado a mano**
  (ver el comentario "Nota de temporizacion" en `GEMM_2x2_HLS_Top.h`) para compensar
  que cada escritura hacia la malla tarda 1 ciclo en propagarse (todo sale de un
  `SC_METHOD` sincrono, a diferencia del testbench de simulación que escribe directo
  desde `sc_main`). Si `cosim_design` da resultados distintos a `csim_design`, es la
  primera sospechosa — comparar contra el timing ya validado en
  `GEMM_2x2_HLS_Top__TB.cpp`.

## 5) Files in this folder

- `run_hls.tcl` — script de automatización principal (top, part, reloj, flujo).
- `gemm_2x2_hls_top.cpp` — unidad de traducción mínima, solo incluye el diseño real
  (`../../Proyecto_SystemC/gemm_hls/GEMM_2x2_HLS_Top.h`), sin duplicarlo.
- (sin testbench propio) — `run_hls.tcl` reutiliza
  `../../Proyecto_SystemC/gemm_hls/GEMM_2x2_HLS_Top__TB.cpp` directamente.

## 6) Common issues

- Part not found: editar `run_hls.tcl` y ajustar `FPGA_PART` (o usar la alternativa de
  board file comentada).
- `vitis_hls: command not found`: entorno no cargado — hacer `source` al
  `settings64.sh` correcto primero.
- Si `csynth_design` falla en el límite de puertos del *top*: confirmar que
  `GEMM_2x2_HLS_Top` sigue exponiendo solo `sc_in<bool>`/`sc_in<sc_int<32>>`/
  `sc_out<bool>`/`sc_out<sc_int<32>>` — cualquier puerto nuevo que se agregue debe
  mantenerse escalar, el empaquetado a `Link` es interno a propósito.
