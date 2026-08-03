# hls_vitis_cgra_2x2_heterogeneous_c

Vitis HLS project para `CGRA_Hetero_2x2_Demo_Top_C`
(`../../Proyecto_C/cgra_hetero_2x2_demo_c/CGRA_Hetero_2x2_Demo_Top_C.h`) —
sintetiza una malla 2x2 **heterogénea** de verdad (`Routing_Cell` + `PE_Memory` +
`PE_Scalar` + `PE_Vector`, cada celda un tipo C++ distinto en la misma malla),
en C/C++ puro, sin SystemC.

## Qué prueba esto

`Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c/` ya probó que una malla C/HLS *homogénea*
(4 `PE_MAC`) sintetiza de punta a punta. Este proyecto prueba el paso que faltaba:
que `Proyecto_C/mesh_hls_c/CGRA_Mesh_Static_C.h` — generalizada de homogénea a
heterogénea (`CellTs...` variádico, storage en cadena de herencia `CellChain`,
dispatch por sobrecarga de `cell_step`/`cell_program`/`cell_clear_acc`) — también
sintetiza con una mezcla real de tipos de celda, no solo con el mismo tipo repetido.

Layout fijo (mismo que el precedente SystemC `CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp`
/ `CGRA_Mesh_Static_2x2_Heterogeneous_Test__TB.cpp`):

```
P00 = Routing_Cell    P01 = PE_Memory
P10 = PE_Scalar       P11 = PE_Vector
```

## Interfaz del top

A diferencia de `GEMM_2x2_HLS_Top_C` (que tiene una FSM de fases propia,
`cgra_run<...>`), este top es deliberadamente crudo — no hay todavía una
aplicación específica que programar con un horario propio:

- `prog_valid`/`prog_row`/`prog_col`/`prog_slot`/`prog_instr`: programa una
  instrucción/config en la celda `(prog_row,prog_col)` — igual convención que
  `mesh_program()` (`slot` es dirección de `instr_mem` para Scalar/Vector, índice
  de contexto de configuración para Routing/Memoria).
- `rst`/`enable` + los 4 bordes de la malla (`in_N/in_S/in_W/in_E`,
  `out_N/out_S/out_W/out_E`): corre **un ciclo** de `mesh_step()` por invocación.

El host (software o testbench) orquesta la secuencia completa llamando al top
varias veces — exactamente como lo hace
`CGRA_Hetero_2x2_Demo_Top_C__TB.cpp`: programa las 4 celdas, presenta los bordes
externos, corre varios ciclos, reprograma la memoria a mitad de camino (round trip)
y verifica el resultado final.

## Escenario validado por el testbench

1. Un valor entra por el borde oeste externo (fila 0) → `Routing_Cell` lo desvía
   hacia `PE_Memory` (DMA NoC(oeste)→SRAM) → se reprograma la memoria para el
   viaje de vuelta (DMA SRAM→NoC(oeste)) → `Routing_Cell` lo saca de nuevo al
   borde oeste externo. Prueba el *round trip* Routing↔Memoria.
2. En paralelo, en la otra diagonal: un valor entra por el borde oeste externo
   (fila 1) → `PE_Scalar` lo combina con una constante y lo manda al este →
   `PE_Vector` lo recibe, le suma otra constante, y lo saca por el borde este
   externo. Prueba el pipeline Scalar→Vector.

## 1) Requirements

### Software
- Vitis HLS 2024.1 (o una versión compatible con el flujo de Vivado)
- Entorno de shell Linux (Ubuntu recomendado)

### Environment setup
```bash
source /path/to/Xilinx/Vitis_HLS/2024.1/settings64.sh
```

## 2) How to run

Desde esta carpeta (`hls_vitis_cgra_2x2_heterogeneous_c/`):

```bash
vitis-run --mode hls --tcl run_hls.tcl
```

(`vitis_hls -f run_hls.tcl` también funciona, pero está deprecado en 2024.1.)

Ejecuta el flujo completo definido en `run_hls.tcl`: `csim_design`,
`csynth_design`, `cosim_design -rtl verilog`, `export_design -format ip_catalog`.

## 3) Expected outputs

- Directorio del proyecto: `cgra_hetero_2x2_prj/`
- Reportes de síntesis: `cgra_hetero_2x2_prj/solution1/syn/report/`
- Artefactos de co-simulación: `cgra_hetero_2x2_prj/solution1/sim/`
- IP exportado: `cgra_hetero_2x2_prj/solution1/impl/ip/`

`CGRA_Hetero_2x2_Demo_Top_C__TB.cpp` imprime PASS/FAIL para el round trip
Routing↔Memoria y el pipeline Scalar→Vector, ya validado con g++ standalone
(sin Vitis) antes de intentar el flujo real.

## 4) Files in this folder

- `run_hls.tcl` — script de automatización principal.
- `cgra_hetero_2x2_top_c.cpp` — unidad de traducción mínima, solo incluye el
  diseño real (`../../Proyecto_C/cgra_hetero_2x2_demo_c/CGRA_Hetero_2x2_Demo_Top_C.cpp`),
  sin duplicarlo.
- (sin testbench propio) — `run_hls.tcl` reutiliza
  `../../Proyecto_C/cgra_hetero_2x2_demo_c/CGRA_Hetero_2x2_Demo_Top_C__TB.cpp` directamente.

## 5) Common issues

- Part not found: editar `run_hls.tcl` y ajustar `FPGA_PART`.
- `vitis_hls: command not found` / `vitis-run: command not found`: entorno no
  cargado — hacer `source` al `settings64.sh` correcto primero.
- Si `csynth_design` se queja de tipos de la STL (p.ej. `<tuple>`/`<array>`):
  el storage heterogéneo de `CGRA_Mesh_Static_C.h` usa una cadena de herencia
  (`CellChain`), no `std::tuple` — el frontend de síntesis de Vitis HLS 2024.1
  rechazó `std::tuple` de plano (`csim_design` sí lo aceptaba). Si se toca ese
  archivo, evitar reintroducir contenedores de la STL.
