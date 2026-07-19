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
│   ├── pe_isa.h                 ISA propio compartido por las 3 variantes
│   ├── PE_Base.h                Contrato de puertos de malla (in/out N/S/E/W, instr_in, clk/rst/enable)
│   ├── scalar/                  PE_scalar: variante base (ALU + PC + banco de registros)
│   ├── vector/                  PE_vector: variante SIMD de ancho fijo (VLEN lanes)
│   └── mac/                     PE_MAC: multiply-accumulate vectorial de 1 ciclo
├── mesh/                        Fabric estructural: instancia y cablea un grid de PEs
├── riscv_dma_main_mem_components/  Modelo de sistema en TLM-2.0 (RiscvCore -> CSR_DMA -> MainMemory)
├── architecture/                Diagramas de arquitectura (.svg)
└── build/                       Directorio de compilación de pe/ + mesh/ (generado, no versionado)
```

Hay dos builds de CMake independientes en el repo:

- **Raíz** (`CMakeLists.txt`) — compila `pe/` y `mesh/` juntos.
- **`riscv_dma_main_mem_components/`** — tiene su propio `CMakeLists.txt` y build
  aparte (todavía no está integrado con `pe/`/`mesh/`).

## Requisitos

- [SystemC](https://systemc.org/) (variable de entorno `SYSTEMC_HOME` apuntando a la
  instalación, si no está en una ruta estándar del sistema).
- CMake >= 3.16 y un compilador con soporte C++17.
- (Opcional) [GTKWave](https://gtkwave.sourceforge.net/) para ver los waveforms
  (`.vcd`) que generan algunos testbenches.

## Compilar

**`pe/` + `mesh/`** (desde la raíz del repo):
```
mkdir -p build && cd build
cmake ..
make
```
Los binarios quedan en `build/pe/` y `build/mesh/`.

**`riscv_dma_main_mem_components/`** (build separado):
```
cd riscv_dma_main_mem_components
mkdir -p build && cd build
cmake ..
make
```

## Ejecutar

Desde `build/pe/`:
```
./ALU_scalar__TB   # vectores de prueba de la ALU escalar (13 opcodes)
./PE_scalar__TB    # smoke test de la PE escalar (routing de malla, enable, rst)
./ALU_vector__TB   # vectores de prueba de la ALU vectorial (13 opcodes, 4 lanes)
./PE_vector__TB    # smoke test de la PE vectorial (routing, enable, rst, broadcast)
./ALU_MAC__TB      # vectores de prueba de la ALU (13 opcodes heredados + MAC)
./PE_MAC__TB       # smoke test de la PE MAC (acumulación de 1 ciclo, SRC_ACC/DST_ACC)
```

Desde `build/mesh/`:
```
./CGRA_Mesh_SmokeTest__TB    # malla 3x1, resumen rápido de los 3 tipos de PE
./CGRA_Mesh_ComplexTest__TB  # malla 3x3, 3 pipelines de 3 etapas independientes
```

Desde `riscv_dma_main_mem_components/build/`:
```
./RiscvDmaSystem__TB   # smoke test de sistema: RiscvCore -> CSR_DMA -> MainMemory -> CGRA_Stub
```

Los testbenches de `PE_*__TB` y `CGRA_Mesh_*__TB` generan además archivos `.vcd`
(waveform) en el directorio desde el que se ejecutan, inspeccionables con `gtkwave`.

## Limpiar y recompilar

```
rm -rf build
mkdir build && cd build
cmake ..
make
```

