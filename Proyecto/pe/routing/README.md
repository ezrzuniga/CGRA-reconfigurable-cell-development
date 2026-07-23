# Routing Cell (celda de enrutamiento / crossbar)

Modelo en SystemC de la celda de enrutamiento de la CGRA: un switch-box de 8 puertos
(`Routing_Cell.h`, 4 hacia celdas de enrutamiento vecinas N/S/E/W + 4 locales hacia la
PE adjunta) con un banco de `RC_NUM_CONTEXTS` (4) configuraciones de ruteo
pre-cargadas, seleccionables en caliente vía `ctx_sel`. `PE_Routing_Cell.h` la
envuelve con el contrato uniforme de `../PE_Base.h` para que pueda ocupar una
posición del grid en `../../mesh/CGRA_Mesh_Heterogeneous.h` igual que las demás
variantes de celda (`CellKind::ROUTING`).

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
Los binarios de esta carpeta quedan en `build/pe/routing/`.

## Ejecutar
Desde `build/` (o `cd pe/routing` dentro de `build/`):
```
./pe/routing/Routing_Cell__TB                  # switch-box aislado: contextos, ctx_sel, reset
./pe/routing/Routing_Cell_PE_Integration__TB   # 2 Routing_Cell + 2 PE_scalar: dato cruzando ambas
```
Ambos generan un `.vcd` con el estado interno de la celda, visible con `gtkwave`.

Para ejercitar `PE_Routing_Cell` ya integrada como una posición del grid en una malla
heterogénea real (junto con Memoria/Escalar/Vectorial), ver
`../../mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp` y su target
`build/mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB`.

## Estructura
- `Routing_Cell.h` — el switch-box en sí: 8 puertos de dato (`Link =
  PE_VectorData<DATA_W,VLEN>`), banco de 4 contextos (`RC_Config`, un selector de 4
  bits por salida) y `ctx_sel` para alternar entre ellos sin recargar.
- `PE_Routing_Cell.h` — adaptador que expone `Routing_Cell` bajo el contrato
  `PE_Base` (in/out N/S/E/W + `instr_in` de 1 parámetro), para que
  `CGRA_Mesh_Heterogeneous` pueda instanciarla como cualquier otra celda. Sin PE local
  adjunta en esta posición del grid, los 4 puertos locales de `Routing_Cell` quedan
  atados a `Link()` fijo (documentado en el propio header). Programación vía
  `instr_in`: el campo `imm` empaqueta los 8 selectores de un `RC_Config` (4 bits cada
  uno), `addr` (2 bits bajos) selecciona el contexto — ver el comentario de cabecera de
  `PE_Routing_Cell.h` para el detalle exacto del empaquetado y un ejemplo de uso está
  en `../../mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp`.
- `Routing_Cell__TB.cpp` — testbench aislado del switch-box (sin PE adjunta).
- `Routing_Cell_PE_Integration__TB.cpp` — testbench con 2 `Routing_Cell` + 2
  `PE_scalar`, verificando que un dato atraviese ambas etapas de enrutamiento hasta
  llegar a la PE del otro extremo.
