# hls_vitis_gemm_2x2_temporal_cgra_c

Vitis HLS project for `GEMM_2x2_Temporal_Top_C`
(`../../Proyecto_C/gemm_temporal_hls_c/GEMM_2x2_Temporal_Top_C.h`) — the
**temporal** mapping of 2×2 matrix multiplication: a single synthesizable
`PE_MAC` cell (`CGRA_Mesh_Static_C<1,1,32,1,PE_MAC_State>`), invoked 4
separate times (once per output element `C[i][j]`). Counterpart to
`../hls_vitis_gemm_2x2_cgra_c` (the 4-cell spatial design that computes the
whole matrix in one invocation).

See `../../Proyecto_C/gemm_temporal_hls_c/README.md` for the full design
writeup.

**Status**: validated end-to-end with Vitis HLS 2024.2 —
`csim_design`/`csynth_design`/`cosim_design`/`export_design` all pass.
Real RTL cosim latency: **51 cycles per output element** (204 cycles for a
full matrix across 4 invocations) vs. the spatial design's **82 cycles for
the whole matrix in one invocation** — spatial is ~2.5× faster here at
~3.7–4.6× the area. See
`../../comparison_matrix_multiplication.md` (repo root) for the full data
analysis, including why this result is the opposite of the sum-reduction
comparison's outcome.

## How to run

```bash
source /path/to/Xilinx/Vitis_HLS/2024.2/settings64.sh
cd Proyecto_HLS/hls_vitis_gemm_2x2_temporal_cgra_c
vitis-run --mode hls --tcl run_hls.tcl
```

Reports land in
`gemm_2x2_temporal_cgra_c_prj/solution1/syn/report/GEMM_2x2_Temporal_Top_C_csynth.rpt`
and real RTL cycle counts in
`gemm_2x2_temporal_cgra_c_prj/solution1/sim/report/verilog/result.transaction.rpt`.

## Files

- `run_hls.tcl` — automation script (top, part, clock, flow).
- `gemm_2x2_temporal_hls_top_c.cpp` — minimal translation unit, only
  includes the real design
  (`../../Proyecto_C/gemm_temporal_hls_c/GEMM_2x2_Temporal_Top_C.cpp`).
- (no local testbench) — reuses
  `../../Proyecto_C/gemm_temporal_hls_c/GEMM_2x2_Temporal_Top_C__TB.cpp`
  directly, same 2 test cases as the spatial design's testbench.
