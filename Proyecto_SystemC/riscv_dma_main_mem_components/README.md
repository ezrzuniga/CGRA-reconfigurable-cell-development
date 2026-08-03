# RISC-V + CSR-DMA + MeshWrapper simulation

This directory contains a SystemC/TLM-2.0 model of a small RISC-V-style software flow connected to a CSR-DMA bridge, main memory, and a real MeshWrapper accelerator.

The main testbench wires together:

- RiscvCore: generates the software test flow and drives the bridge
- CSR_DMA: forwards configuration, start, input, and output transactions
- MainMemory: stores the input/output payloads
- MeshWrapper: a real 2x2 heterogeneous CGRA array (Routing, Memory, Scalar, Vector
  cells — the same layout as `Entrega_Avance_2/images/lvl2_diagram.png`), executing
  whichever `MeshProgram` the configured `cgra_config` selects (see
  `../mesh_wrapper/README.md`, "Catálogo de programas")

## Key files

- RiscvDmaSystem__TB.cpp: top-level testbench that instantiates the full system
- riscv_core.cpp / riscv_core.h: RISC-V software model and vector-add test logic
- csr_dma.cpp / csr_dma.h: CSR-DMA bridge implementation
- main_memory.cpp / main_memory.h: simple memory model
- ../mesh_wrapper/mesh_wrapper.cpp / mesh_wrapper.h: accelerator wrapper used by the simulation

## What the simulation does

The included testbench runs two kernels back-to-back through the exact same
RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper chain:

- **Vector add** (`VECTOR_ADD`): two 4-lane int32 vectors are added elementwise
  entirely on the Vector cell — `c = a + b`
- **Full pipeline** (`FULL_PIPELINE`): exercises all 4 CGRA cells — operand `b`
  travels Routing -> Memory (a real DMA round trip through the memory cell's SRAM)
  -> Routing -> Scalar (which computes `b*2`), while operand `a` goes straight to
  the Vector cell; Vector computes the final `e = a + b*2`

For each kernel: the RISC-V core prepares the input vectors, the bridge passes them
to the accelerator through the memory-mapped interface, MeshWrapper executes the
selected program, and the output is printed and checked against the expected
values.

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

A successful run prints the input vectors and accelerator output for each kernel, and ends with:

```text
VECTOR ADD TEST PASSED.
...
FULL PIPELINE TEST PASSED.
PASS: RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper end-to-end smoke test (vector-add + full 2x2 heterogeneous pipeline).
```
