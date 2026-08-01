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
acumulador), mismo wiring N/S/E/W explícito de la malla, misma secuencia de
fases (limpiar acumuladores → recargar programa → fase 0 → fase 1 → lectura)
— ver `Proyecto/pe_hls_c/`, `Proyecto/mesh_hls_c/`, `Proyecto/gemm_hls_c/` y
el comentario de cabecera de `GEMM_2x2_HLS_Top_C.h` para el detalle de cada
decisión de la migración (por qué se pudo saltar el arranque de una sola vez
del `sc_module` original, y cómo se preserva la disciplina de "escritura este
ciclo, visible el ciclo siguiente" entre la FSM y la malla sin sc_signal).

Diferencia de interfaz respecto al original: `GEMM_2x2_HLS_Top_C` no es un
`sc_module` con puertos `clk`/`rst`/`enable` — es una función plana que
corre la secuencia completa de ciclos en una sola invocación y retorna con
`done=true` (ver decisión de diseño en el plan de migración). `start`/`done`
siguen presentes como argumentos, pero como pulso de una sola llamada, no
como handshake entre invocaciones separadas.

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
  testbench plano (sin `sc_main`) que llama la función top directamente y
  compara contra los mismos 2 casos de prueba que el resto de testbenches
  de GEMM 2x2 del repo.
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
  diseño real (`../../Proyecto/gemm_hls_c/GEMM_2x2_HLS_Top_C.h`), sin
  duplicarlo.
- (sin testbench propio) — `run_hls.tcl` reutiliza
  `../../Proyecto/gemm_hls_c/GEMM_2x2_HLS_Top_C__TB.cpp` directamente.

## 5) Common issues

- Part not found: editar `run_hls.tcl` y ajustar `FPGA_PART` (o usar la
  alternativa de board file comentada).
- `vitis_hls: command not found` / `vitis-run: command not found`: entorno no
  cargado — hacer `source` al `settings64.sh` correcto primero.
