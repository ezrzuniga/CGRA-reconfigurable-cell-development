# cgra_final_noc/vitis/ — síntesis Vitis HLS de la CGRA NoC real

Cuatro proyectos Vitis HLS 2024.1, uno por kernel (`gemm/`, `sum_reduce8/`,
`fft4/`, `softmax4/`), que sintetizan **la malla 3×3 heterogénea real**
(`CGRA_Final_NoC_Mesh_C` — Memoria/Vectorial/Escalar/Routing/MAC sobre la
fábrica de routers NoC) ejecutando el programa espacial de cada kernel, no
una reimplementación aritmética aparte. El diseño real vive en
`../../../Proyecto_C/cgra_final_noc_c/<Kernel>_NoC_HLS_Top_C.{h,cpp}`; cada
carpeta de acá solo tiene una unidad de traducción mínima
(`<kernel>_noc_hls_top_c.cpp`, un `#include` sin duplicar nada) y su
`run_hls.tcl`.

## Por qué esto y no un kernel aritmético

`../../../Proyecto_HLS/hls_vitis_sum_reduction/` ya muestra la alternativa
más simple (kernel `total = seed + sum(v)`, sin modelar la malla). Acá se
eligió sintetizar la malla NoC de verdad porque el punto de este repo es la
arquitectura del acelerador (routers, celdas heterogéneas, ISA de PE) — un
kernel aritmético no dice nada sobre esa arquitectura. Cada top instancia un
`static CGRA_Final_NoC_Mesh_C mesh;` (persiste entre llamadas — la malla se
programa una única vez y corre muchas veces, como firmware real cargado una
vez) y reproduce, ciclo a ciclo, el mismo programa espacial + calendario que
ya validan los testbenches SystemC/C de `../` y
`../../../Proyecto_C/cgra_final_noc_c/` (`arm_case` → alimentar operandos →
conmutar Routing a `ctx1` → leer resultado).

## Bugs de síntesis encontrados y corregidos (afectan a TODO el árbol `Proyecto_C/`, no solo a estos 4 kernels)

Antes de estos 4 proyectos, nadie había corrido `csynth_design` contra una
instalación real de Vitis HLS sobre la malla 3×3 completa (`pe_hls_c/`,
`memory_hls_c/`, `mesh_hls_c/`) — los proyectos existentes que la usan
(`Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c/`) solo estaban validados con
`csim_design` (g++ real), y esa carpeta lo dice explícitamente en su propio
README. Al correr `csynth_design` de verdad aparecieron dos bugs reales,
ambos corregidos en los headers compartidos (`pe_isa_hls_c.h`,
`memory_hls_c/PE_Memory_HLS_C.h`, `pe_hls_c/{vector,scalar,routing,mac}/*.h`):

1. **`#pragma HLS ARRAY_PARTITION`/`BIND_STORAGE` en el cuerpo del struct**
   (no dentro de una función): Vitis HLS 2024.1 lo rechaza con
   `'#pragma HLS' is only allowed in function scope`. `csim_design` no lo
   detecta porque corre con g++, que ignora pragmas que no entiende — el
   error solo aparece con el frontend de síntesis real. Arreglo: mover cada
   pragma al cuerpo del constructor del struct (mismo efecto de síntesis,
   scope de función).
2. **`#pragma HLS INLINE` + `#pragma HLS PIPELINE II=1` en la misma
   función**: Vitis HLS 2024.1 rechaza la combinación
   (`Pragma conflict happens on 'INLINE' and 'PIPELINE' pragmas`) en las 5
   funciones `*_step` de las celdas (`pe_mac_step`, `pe_vector_step`,
   `pe_scalar_step`, `routing_cell_step_local`, `memory_step`). Arreglo:
   quitar el `PIPELINE` redundante — el II=1 real ya lo aporta el
   `#pragma HLS PIPELINE II=1` de `mesh_step()`
   (`mesh_hls_c/CGRA_Mesh_Static_C.h`), que envuelve estas funciones ya
   inlineadas junto con las demás ROWS×COLS-1 celdas.

Ninguno de los dos cambia comportamiento (`csim_design` sigue dando PASS
idéntico antes y después, ver los 4 testbenches en
`../../../Proyecto_C/cgra_final_noc_c/*__TB.cpp`) — son correcciones de
sintaxis/scope de pragma, no de lógica.

## 1) Requirements

- Vitis HLS 2024.1 (`source /ruta/a/Xilinx/Vitis_HLS/2024.1/settings64.sh`)
- Linux, `vitis-run` en el `PATH` tras el `source`

## 2) Cómo correr

Desde cada subcarpeta (`gemm/`, `sum_reduce8/`, `fft4/`, `softmax4/`):

```bash
vitis-run --mode hls --tcl run_hls.tcl
```

Cada `run_hls.tcl` corre el flujo completo: `csim_design` → `csynth_design`
→ `cosim_design -rtl verilog` → `export_design` (IP catalog). Target: AMD
Kria KV260 (`xck26-sfvc784-2LV-c`), reloj 250 MHz (período 4 ns).

## 3) Resultados de síntesis (csynth_design, ya corridos)

Los 4 kernels comparten el mismo Fmax estimado (223.59 MHz — el camino
crítico de la malla NoC 9-celdas/9-routers domina sobre el programa
espacial de cada kernel):

| Kernel      | Latencia (ciclos) min–max | Reloj estimado | LUT (117120 disp.) | FF (234240 disp.) | DSP (1248 disp.) |
|-------------|---------------------------|-----------------|---------------------|---------------------|--------------------|
| GEMM 2×2    | 58 – 236                  | 4.473 ns        | 161196 (**138%**)   | 35553 (15%)         | 189 (15%)          |
| SumReduce8  | 67 – 193                  | 4.473 ns        | 186955 (**160%**)   | 37120 (16%)         | 192 (15%)          |
| FFT4        | 62 – 171                  | 4.473 ns        | 133375 (**114%**)   | 32414 (14%)         | 144 (12%)          |
| Softmax4    | 53 – 165                  | 4.473 ns        | 125489 (**107%**)   | 25412 (11%)         | 96 (8%)            |

**Hallazgo real, no ocultado**: los 4 kernels exceden el presupuesto de LUT
del KV260 (107%–160%) tal como están sintetizados, aunque DSP/FF/BRAM sobran
con margen. Causa: `step_n()` (bucle que llama a `cgra_final_noc_step()`,
`#pragma HLS INLINE off`) se invoca desde varios call-sites con distinto
conteo literal de iteraciones (p.ej. GEMM: 1, 4, 4, 1, 1) — el reporte de
`Instance` (`.../syn/report/<Top>_csynth.rpt`) muestra que Vitis HLS generó
**más de una realización de hardware** de la lógica de paso de malla en vez
de compartir una sola instancia entre todos los call-sites. Rango de
latencia (min vs. max) refleja el mismo motivo: la primera invocación de
cada top carga el programa espacial completo (`if (!programmed)`, canal de
`noc_mesh_program()` no pipelineado), las siguientes solo corren el caso.
Próximo paso razonable (no aplicado acá): forzar compartir la instancia con
`#pragma HLS ALLOCATION instances=cgra_final_noc_step limit=1 function` o
reestructurar `step_n()` para que todos los call-sites pasen por una única
región de control.

Co-simulación RTL (`cosim_design`) y export de IP (`export_design`) quedan
definidos en cada `run_hls.tcl` — son el paso más lento del flujo (lanzan un
simulador RTL); correrlos es cuestión de tiempo de máquina, no de código.

## 4) Files por carpeta

- `run_hls.tcl` — script de automatización (top, part, reloj, flujo).
- `<kernel>_noc_hls_top_c.cpp` — unidad de traducción mínima, solo incluye el
  diseño real de `../../../Proyecto_C/cgra_final_noc_c/`.
- (sin testbench propio) — cada `run_hls.tcl` reutiliza
  `../../../Proyecto_C/cgra_final_noc_c/<Kernel>_NoC_HLS_Top_C__TB.cpp`
  directamente (mismos casos de prueba, misma semilla `20260810`, que los
  testbenches SystemC/C de `../` y `../../../Proyecto_C/`).

## 5) Common issues

- Part not found: editar `run_hls.tcl` y ajustar `FPGA_PART` (o usar la
  alternativa de board file comentada).
- `vitis_hls`/`vitis-run: command not found`: falta `source` al
  `settings64.sh` correcto.
- `csynth_design` con `'#pragma HLS' is only allowed in function scope` o
  `Pragma conflict... 'INLINE' and 'PIPELINE'`: ya corregido en
  `Proyecto_C/` (ver sección de arriba) — si reaparece en una celda nueva,
  aplicar el mismo patrón (pragma de partición dentro del constructor, sin
  `PIPELINE` en funciones ya marcadas `INLINE`).
