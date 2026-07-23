# RISC-V + CSR-DMA + MeshWrapper simulation

This directory contains a SystemC/TLM-2.0 model of a small RISC-V-style software flow connected to a CSR-DMA bridge, main memory, and a real MeshWrapper accelerator.

The main testbench wires together:

- RiscvCore: generates the software test flow and drives the bridge
- CSR_DMA: forwards configuration, start, input, and output transactions
- MainMemory: stores the input/output payloads
- MeshWrapper: executes the configured program (including the vector-add path)

## Key files

- RiscvDmaSystem__TB.cpp: top-level testbench that instantiates the full system
- riscv_core.cpp / riscv_core.h: RISC-V software model and vector-add test logic
- csr_dma.cpp / csr_dma.h: CSR-DMA bridge implementation
- main_memory.cpp / main_memory.h: simple memory model
- ../mesh_wrapper/mesh_wrapper.cpp / mesh_wrapper.h: accelerator wrapper used by the simulation

## What the simulation does

The included testbench runs a vector-add smoke test:

- the RISC-V core prepares two 4-lane int32 vectors
- the bridge passes them to the accelerator through the memory-mapped interface
- the MeshWrapper executes the vector-add program and returns the result
- the output is printed and checked against the expected values

## Prerequisites

- CMake 3.16 or newer
- A C++17-capable compiler
- SystemC installed and discoverable by CMake
  - either via the SYSTEMC_HOME environment variable, or
  - through a standard installation path such as /usr/local/systemc

## Build

From the repository root:

```bash
cd /home/jeffrey/cgra_system/CGRA-reconfigurable-cell-development
cmake -S riscv_dma_main_mem_components -B build-riscv-dma
cmake --build build-riscv-dma -j2
```

## Run

```bash
cd /home/jeffrey/cgra_system/CGRA-reconfigurable-cell-development
./build-riscv-dma/RiscvDmaSystem__TB
```

## Expected output

A successful run prints the input vectors, the accelerator output, and ends with a success message such as:

```text
VECTOR ADD TEST PASSED.
PASS: RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper vector-add smoke test.
```
