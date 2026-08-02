# hls_vitis_gemm_2x2_cgra_c

Vitis HLS project para `GEMM_2x2_HLS_Top_C`
(`../../Proyecto/gemm_hls_c/GEMM_2x2_HLS_Top_C.h`) — sintetiza la malla real
(`CGRA_Mesh_Static_C<2,2,32,1, 4 celdas PE_MAC>`), en C/C++ puro (sin
SystemC), no una reimplementación aritmética aparte.

## Por qué existe esta carpeta además de `hls_vitis_gemm_2x2_cgra`

`../hls_vitis_gemm_2x2_cgra` (rama `cgra_hls`) sintetiza el mismo diseño pero
escrito como `sc_module` (`GEMM_2x2_HLS_Top`, `pe_hls/`, `mesh_hls/`). Al
correrlo contra una instalación real de Vitis HLS 2024.1, `csynth_design`
falla con:

```
ERROR: [HLS 200-637] SystemC input is not supported
SystemC is not supported!
```

(ver `../hls_vitis_gemm_2x2_cgra/vitis_hls.log:35`). Esto no es un problema de
flags/pragmas: el flujo unificado de Vitis HLS rechaza categóricamente
cualquier top escrito como `sc_module` — la síntesis SystemC que sí soportaba
Vivado HLS clásico fue discontinuada. `csim_design` de esa carpeta sí pasa
(corre con g++ + libSystemC real, no con el frontend de síntesis), por eso el
problema no se detectó antes de tener `vitis_hls` real disponible.

Esta carpeta (`_c`) es la migración de esa misma arquitectura a C/C++ plano +
pragmas HLS: mismo datapath por PE (memoria de instrucciones, ALU/MAC,
acumulador), mismo wiring N/S/E/W explícito de la malla — ver
`Proyecto/pe_hls_c/`, `Proyecto/mesh_hls_c/`, `Proyecto/gemm_hls_c/`.

## Arquitectura: CGRA reprogramable, no un programa fijo en el hardware

`GEMM_2x2_HLS_Top_C` ya no es un `sc_module` con puertos `clk`/`rst`/`enable`,
y tampoco carga un programa fijo internamente. Es un wrapper delgado sobre un
**template genérico reutilizable**, `cgra_run<...>` (`Proyecto/cgra_hls_c/
CGRA_Top_C.h`), parametrizado por `ROWS/COLS/DATA_W/VLEN/NUM_REGS/
INSTR_MEM_SIZE/NUM_PHASES` — de ahí se "saca" cualquier CGRA sintetizable
concreta con el mínimo código posible (ver el header de `GEMM_2x2_HLS_Top_C.h`
y `GEMM_2x2_HLS_Top_C.cpp`, unas 25 líneas en total).

La malla vive en un `static GemmMesh_C mesh;` dentro del `.cpp` del top — el
único estado con memoria del diseño, persiste entre invocaciones separadas.
Esto habilita dos caminos de control, mutuamente excluyentes por llamada:

1. **Programar** (`prog_valid=true`, `prog_row`/`prog_col`/`prog_slot`/
   `prog_instr`): escribe una única instrucción en la memoria de
   instrucciones de la PE `(prog_row,prog_col)`, slot `prog_slot`. `done=true`
   de inmediato. El host sube el programa espacial completo con una llamada
   por instrucción (16 llamadas para GEMM 2x2: 4 PEs × 4 slots).
2. **Correr** (`start=true`): corre las `NUM_PHASES` fases completas en una
   sola invocación, usando el programa YA RESIDENTE en `instr_mem` (subido
   antes vía el paso 1) — se puede disparar muchas veces con operandos
   distintos SIN volver a programar, igual que hardware reconfigurable real
   (firmware cargado una vez, corrido muchas veces). El testbench
   (`GEMM_2x2_HLS_Top_C__TB.cpp`) demuestra esto explícitamente: programa la
   malla una vez y corre los 2 casos de prueba sin reprogramar entre ellos.

Puertos de borde genéricos: `in_N/in_S/in_W/in_E` (entrada, con un eje extra
de fase: `in_*[NUM_PHASES][...]`) y `out_N/out_S/out_W/out_E` (salida),
dimensionados por `GEMM_ROWS`/`GEMM_COLS` — los 4 bordes completos de la
malla, siempre, aunque GEMM 2x2 solo use oeste/norte de entrada y oeste/este
de salida (sur/este de entrada y norte/sur de salida quedan en cero, sin usar
— máxima fidelidad al template en vez de puertos con nombre fijo por
aplicación como `a00`/`b00`/`c00`).

Ver el comentario de cabecera de `Proyecto/cgra_hls_c/CGRA_Top_C.h` para el
detalle completo de la FSM (por qué ya no hace falta un estado de "recargar
programa", cómo se limpia el acumulador con un canal directo en vez de pedir
prestada `instr_mem`, y cómo se preserva la disciplina de "escritura este
ciclo, visible el ciclo siguiente" entre la FSM y la malla sin `sc_signal`).

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

Desde esta carpeta (`hls_vitis_gemm_2x2_cgra_c/`):

```bash
vitis-run --mode hls --tcl run_hls.tcl
```

(`vitis_hls -f run_hls.tcl` también funciona, pero el binario `vitis_hls` está
marcado como deprecado en 2024.1 a favor de `vitis-run --mode hls`.)

Esto ejecuta el flujo completo definido en `run_hls.tcl`:
- C Simulation (`csim_design`) — corre `GEMM_2x2_HLS_Top_C__TB.cpp`, un
  testbench plano (sin `sc_main`) que programa la malla vía `prog_valid`
  (16 llamadas) y luego corre los mismos 2 casos de prueba que el resto de
  testbenches de GEMM 2x2 del repo, sin volver a programar entre ellos.
- C Synthesis (`csynth_design`)
- C/RTL Co-simulation (`cosim_design -rtl verilog`)
- IP export (`export_design -format ip_catalog`)

## 3) Expected outputs

- Directorio del proyecto: `gemm_2x2_cgra_c_prj/`
- Reportes de síntesis: `gemm_2x2_cgra_c_prj/solution1/syn/report/`
- Artefactos de co-simulación: `gemm_2x2_cgra_c_prj/solution1/sim/`
- IP exportado: `gemm_2x2_cgra_c_prj/solution1/impl/ip/`

`GEMM_2x2_HLS_Top_C__TB.cpp` imprime PASS/FAIL por caso (2 casos: enteros
positivos y con negativos, 4 valores de `C` cada uno) y un resumen final.
Ya validado con g++ standalone (sin Vitis, usando `ap_int.h` de la
instalación local de Vitis HLS) antes de intentar el flujo real.

## 4) Files in this folder

- `run_hls.tcl` — script de automatización principal (top, part, reloj, flujo).
- `gemm_2x2_hls_top_c.cpp` — unidad de traducción mínima, solo incluye el
  diseño real (`../../Proyecto/gemm_hls_c/GEMM_2x2_HLS_Top_C.cpp`), sin
  duplicarlo. Los includes son relativos (`../mesh_hls_c/`, `../cgra_hls_c/`,
  `../pe_hls_c/`), no hace falta ningún `-I` adicional en `run_hls.tcl`.
- (sin testbench propio) — `run_hls.tcl` reutiliza
  `../../Proyecto/gemm_hls_c/GEMM_2x2_HLS_Top_C__TB.cpp` directamente.

## 5) Cómo instanciar una CGRA propia a partir del template

Cualquier aplicación nueva repite el mismo patrón de `GEMM_2x2_HLS_Top_C.h`/
`.cpp` (~25 líneas): declarar sus propias constantes `ROWS/COLS/DATA_W/VLEN/
NUM_REGS/INSTR_MEM_SIZE/NUM_PHASES`, un `static Mesh mesh;` dentro de su
`.cpp`, y reenviar sus puertos a `cgra_run<...>(mesh, ...)`
(`Proyecto/cgra_hls_c/CGRA_Top_C.h`) — toda la FSM de fases, la interfaz de
programación y el wiring de bordes se reutilizan sin cambios.

## 6) Common issues

- Part not found: editar `run_hls.tcl` y ajustar `FPGA_PART` (o usar la
  alternativa de board file comentada).
- `vitis_hls: command not found` / `vitis-run: command not found`: entorno no
  cargado — hacer `source` al `settings64.sh` correcto primero.
