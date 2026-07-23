# CGRA-reconfigurable-cell-development

Proyecto final del curso "Diseño de alto nivel de sistemas electrónicos" del
Tecnológico de Costa Rica. Modelos en SystemC (C++17) de los bloques de una CGRA
(Coarse-Grained Reconfigurable Array): las celdas de cómputo (Processing Elements),
la malla que las interconecta, y el sistema RISC-V + DMA que eventualmente las
alimenta.

## Estructura de directorios

```
.
├── pe/                          IP de los Processing Elements (PE)
│   ├── pe_isa.h                 ISA propio compartido por todas las variantes
│   ├── PE_Base.h                Contrato de puertos de malla (in/out N/S/E/W, instr_in, clk/rst/enable)
│   ├── scalar/                  PE_scalar: variante base (ALU + PC + banco de registros)
│   ├── vector/                  PE_vector: variante SIMD de ancho fijo (VLEN lanes)
│   ├── mac/                     PE_MAC: multiply-accumulate vectorial de 1 ciclo
│   └── routing/                 Routing_Cell: switch-box de enrutamiento (crossbar, 4 contextos)
├── memory/                      Celda de memoria: SRAM dual-port + DMA local + adaptador de malla
├── mesh/                        Fabric estructural: instancia y cablea un grid heterogéneo de celdas
├── mesh_wrapper/                Puente TLM-2.0 <-> señales planas sobre una malla real
├── riscv_dma_main_mem_components/  Modelo de sistema en TLM-2.0 (RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper)
├── architecture/                Diagramas de arquitectura (.svg)
└── build/                       Directorio de compilación unificado (generado, no versionado)
```

Layout de referencia (nivel 2, ver `Entrega_Avance_2/images/lvl2_diagram.png`): un
arreglo CGRA 2×2 con una celda de cada tipo heterogéneo —
`CGRA_Mesh_2x2_Heterogeneous_Test__TB` en `mesh/` es el testbench que reproduce ese
layout exacto (Enrutamiento/Memoria arriba, Escalar/Vectorial abajo) y ejercita sus
enlaces internos de extremo a extremo.

El build de CMake está **unificado**: el `CMakeLists.txt` raíz (dentro de `Proyecto/`)
hace `add_subdirectory` de `pe/` (que a su vez incluye `pe/routing/`), `mesh/`,
`mesh_wrapper/`, `riscv_dma_main_mem_components/` y `memory/`. Un solo
`cmake .. && make` desde `build/` compila todo.

## Requisitos

- [SystemC](https://systemc.org/) con TLM-2.0 (variable de entorno `SYSTEMC_HOME`
  apuntando a la instalación, si no está en una ruta estándar del sistema, p.ej.
  `export SYSTEMC_HOME=/usr/local/systemc`).
- CMake >= 3.16 y un compilador con soporte C++17.
- (Opcional) [GTKWave](https://gtkwave.sourceforge.net/) para ver los waveforms
  (`.vcd`) que generan algunos testbenches.

## Compilar

Desde `Proyecto/` (raíz del código, un nivel debajo de este README):
```
export SYSTEMC_HOME=/ruta/a/tu/instalacion/systemc   # si no está en una ruta estándar
cd Proyecto
mkdir -p build && cd build
cmake ..
make -j4
```
Todos los binarios quedan bajo `build/<carpeta>/` (mismo layout que el código fuente).

## Ejecutar

Desde `Proyecto/build/`, cada testbench es un binario independiente (ejecutar desde
ahí para que las rutas relativas de los `.vcd` de salida caigan en ese directorio):

**`pe/`** — ALUs y smoke tests de cada variante de PE:
```
./pe/ALU_scalar__TB   # vectores de prueba de la ALU escalar (13 opcodes)
./pe/PE_scalar__TB    # smoke test de la PE escalar (routing de malla, enable, rst)
./pe/ALU_vector__TB   # vectores de prueba de la ALU vectorial (13 opcodes, 4 lanes)
./pe/PE_vector__TB    # smoke test de la PE vectorial (routing, enable, rst, broadcast)
./pe/ALU_MAC__TB      # vectores de prueba de la ALU (13 opcodes heredados + MAC)
./pe/PE_MAC__TB       # smoke test de la PE MAC (acumulación de 1 ciclo, SRC_ACC/DST_ACC)
```

**`pe/routing/`** — celda de enrutamiento (ver `Proyecto/pe/routing/README.md`):
```
./pe/routing/Routing_Cell__TB                  # switch-box aislado: contextos, ctx_sel, reset
./pe/routing/Routing_Cell_PE_Integration__TB   # 2 Routing_Cell + 2 PE_scalar encadenadas
```

**`memory/`** — celda de memoria (ver `Proyecto/memory/README.md`):
```
./memory/SRAM_dual_port__TB               # acceso concurrente por los 2 puertos de la SRAM
./memory/PE_Memory_Cell__TB                # smoke test manual de PE_Memory_Cell
./memory/PE_Memory_Cell_unit_test          # transferencia DMA de un contexto (ctest)
./memory/PE_Memory_Cell_interconnect_test  # interconexión SRAM<->DMA<->NoC (ctest)
```

**`mesh/`** — mallas homogéneas y la malla 2×2 heterogénea completa:
```
./mesh/CGRA_Mesh_SmokeTest__TB              # malla 3x1, resumen rápido de 3 tipos de PE
./mesh/CGRA_Mesh_ComplexTest__TB            # malla 3x3, 3 pipelines de 3 etapas independientes
./mesh/CGRA_Mesh_1x1_Test__TB               # malla mínima, 1 sola PE_vector
./mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB # malla 2x2: Enrutamiento+Memoria+Escalar+Vectorial
```

**`mesh_wrapper/`** — puente TLM-2.0 sobre una malla real:
```
./mesh_wrapper/MeshWrapper_CSRDMA_Sim__TB
```

**`riscv_dma_main_mem_components/`** — sistema completo:
```
./riscv_dma_main_mem_components/RiscvDmaSystem__TB   # RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper
```

Todos los testbenches además generan un `.vcd` (waveform) en el directorio desde el
que se ejecutan, inspeccionable con `gtkwave <archivo>.vcd`. Alternativamente, correr
todo lo que esté registrado con `add_test` vía CTest:
```
ctest --test-dir Proyecto/build
```

## Limpiar y recompilar

```
rm -rf build
mkdir build && cd build
cmake ..
make
```

