# hls_vitis_sum_reduction

Vitis HLS project for `sum_reduction_kernel`, a synthesizable re-implementation
of the arithmetic performed by `PROGRAM_SUM_REDUCTION` in
[`../mesh_wrapper/mesh_wrapper.cpp`](../mesh_wrapper/mesh_wrapper.cpp)
(`MeshWrapper::run_sum_reduction_dataflow`): `total = seed + sum(v[0..6])`.

## Why this is a separate kernel, not the SystemC module itself

`mesh_wrapper.cpp`/`.h` and its testbenches
([`MeshWrapper_SumReduction__TB.cpp`](../mesh_wrapper/MeshWrapper_SumReduction__TB.cpp))
are SystemC TLM-2.0: `tlm_utils::simple_target_socket`,
`tlm::tlm_generic_payload`, a self-instantiated `sc_clock`, VCD tracing,
`std::vector`. None of that is synthesizable HLS C/C++/SystemC — Vitis HLS's
`csynth_design` cannot turn TLM sockets or generic payloads into RTL. That
design keeps living in the SystemC/CMake build at the repo root (`../mesh`,
`../mesh_wrapper`, `../pe`), unmodified.

This folder instead keeps only the arithmetic contract of
`PROGRAM_SUM_REDUCTION` — the part that's actually meaningful to synthesize —
as a plain C++ function with `s_axilite` ports, following the same flow as
`image_processor_222/HSL_Vitis` (`gray_kernel.cpp` / `run_hls.tcl`).

`MeshWrapper_SumReduction__TB.cpp::FakeCsrDma::run` is the golden reference:
its three `(seed, v, expected_total)` rounds are reproduced verbatim in
[`tb_sum_reduction.cpp`](tb_sum_reduction.cpp).

## 1) Requirements

### Software
- Vitis HLS 2024.1 (or a compatible version with Vivado flow support)
- Linux shell environment (Ubuntu recommended)

### Environment setup
Before running HLS, load the Xilinx tools environment, e.g.:

```bash
source /path/to/Xilinx/Vitis_HLS/2024.1/settings64.sh
```

## 2) How to run

From this directory (`hls_vitis_sum_reduction/`):

```bash
vitis_hls -f run_hls.tcl
```

This executes the full scripted flow defined in `run_hls.tcl`:
- C Simulation (`csim_design`)
- C Synthesis (`csynth_design`)
- C/RTL Co-simulation (`cosim_design -rtl verilog`)
- IP export (`export_design -format ip_catalog`)

## 3) Expected outputs

- Project directory: `sum_reduction_kernel_prj/`
- Synthesis reports: `sum_reduction_kernel_prj/solution1/syn/report/`
- Co-simulation artifacts: `sum_reduction_kernel_prj/solution1/sim/`
- Exported IP: `sum_reduction_kernel_prj/solution1/impl/ip/`

`tb_sum_reduction.cpp` prints PASS/FAIL per round and an overall
`RESULTADO: PASS/FAIL`, matching the console output style of
`MeshWrapper_SumReduction__TB.cpp`.

## 4) Files in this folder

- `run_hls.tcl` — main automation script (top function, part, clock, flow stages).
- `sum_reduction_kernel.h` / `.cpp` — the HLS kernel (`total = seed + sum(v)`, `s_axilite` interface).
- `tb_sum_reduction.cpp` — C testbench; golden values copied from `MeshWrapper_SumReduction__TB.cpp`.

## 5) Common issues

- Part not found: edit `run_hls.tcl` and adjust `set FPGA_PART` (or use the
  commented board-file alternative).
- `vitis_hls: command not found`: environment not loaded — source the correct
  `settings64.sh` first.
