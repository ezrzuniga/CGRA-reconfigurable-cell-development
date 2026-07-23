# Memory Cell (celda de memoria)

Modelo en SystemC de la celda de memoria de la CGRA: SRAM dual-port local
(`SRAM_dual_port.h`), un DMA local con 4 contextos de configuración
(`Local_DMA.h` + `Access_controller.h`, acceso directo o con stride) y
`PE_Memory_Cell.h`, que integra ambos detrás de dos interfaces TLM-2.0: un
`csr_socket` (configuración de los 4 contextos + registro de control/estado) y un
par `noc_target_socket`/`noc_initiator_socket` (tráfico de datos hacia el resto de
la malla). `PE_Memory_Mesh_Cell.h` la envuelve con el contrato uniforme de
`../pe/PE_Base.h` para que pueda ocupar una posición del grid en
`../mesh/CGRA_Mesh_Heterogeneous.h` igual que las demás variantes de celda
(`CellKind::MEMORY`) — es el puente entre el mundo TLM de `PE_Memory_Cell` y las
señales planas (`Link`) del resto de la malla.

## Requisitos
- SystemC con TLM-2.0 (variable de entorno `SYSTEMC_HOME` apuntando a la
  instalación, si no está en una ruta estándar del sistema).
- CMake >= 3.16 y un compilador con soporte C++17.
- (Opcional) [GTKWave](https://gtkwave.sourceforge.net/) para ver el waveform generado.

## Compilar
Desde la raíz del repo (el build está unificado, ver `CMakeLists.txt` raíz):
```
mkdir -p build && cd build
cmake ..
make
```
Los binarios de esta carpeta quedan en `build/memory/`.

## Ejecutar
Desde `build/` (o `cd memory` dentro de `build/`):
```
./memory/SRAM_dual_port__TB               # acceso concurrente por los 2 puertos de la SRAM
./memory/PE_Memory_Cell__TB                # smoke test manual de PE_Memory_Cell (config + DMA)
./memory/PE_Memory_Cell_unit_test          # transferencia DMA de un contexto, vía ctest
./memory/PE_Memory_Cell_interconnect_test  # interconexión SRAM<->DMA<->NoC, vía ctest
```
También se puede correr por CTest (usa los mismos binarios):
```
ctest --test-dir memory
```

Para ejercitar `PE_Memory_Mesh_Cell` ya integrada como una posición del grid en una
malla heterogénea real (junto con Enrutamiento/Escalar/Vectorial), ver
`../mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp` y su target
`build/mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB`.

## Estructura
- `SRAM_dual_port.h` — memoria local (1–4 KB configurable), 2 puertos TLM
  independientes.
- `Access_controller.h` — genera la secuencia de direcciones de una transferencia,
  modo directo (1 palabra) o con stride (`count` palabras cada `stride` bytes).
- `Local_DMA.h` — mueve datos entre los 2 puertos de la SRAM y el puerto NoC
  (SRAM→NoC, NoC→SRAM o SRAM→SRAM), usando un `AccessController` por extremo.
- `PE_Memory_Cell.h` — integra SRAM + DMA detrás de un mapa de registros TLM de 4
  contextos (`0x00–0x14` por contexto, `0x80` control, `0x84` estado) más el par de
  sockets NoC. IP validada de forma aislada, sin cambios para la integración a la
  malla.
- `PE_Memory_Mesh_Cell.h` — adaptador `PE_Base` sobre `PE_Memory_Cell`: traduce el
  puerto `instr_in` uniforme de la malla en escrituras TLM sobre `csr_socket`
  (`MemCellField` documenta el mapeo de campos), y ata el único puerto NoC de la
  celda al borde oeste (`in_W`/`out_W`) — ver el comentario de cabecera del propio
  header, sección "Simplificaciones conscientes", para el porqué de esa elección y
  sus límites (Norte/Sur/Este quedan cableados pero inertes).
- `PE_Memory_Cell__TB.cpp` / `PE_Memory_Cell_unit_test.cpp` /
  `PE_Memory_Cell_interconnect_test.cpp` — testbenches de `PE_Memory_Cell` en
  aislamiento (sin la malla).
- `SRAM_dual_port__TB.cpp` — testbench de la SRAM en aislamiento.
