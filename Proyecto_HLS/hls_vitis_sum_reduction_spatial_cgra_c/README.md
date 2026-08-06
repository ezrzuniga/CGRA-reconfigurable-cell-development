# hls_vitis_sum_reduction_spatial_cgra_c

Vitis HLS project for `SumReduction_Spatial_Top_C`
(`../../Proyecto_C/sum_reduction_hls_c/SumReduction_Spatial_Top_C.h`) — the
**spatial** mapping of sum reduction: a real synthesizable CGRA mesh
(`CGRA_Mesh_Static_C<2,2,32,1, 4x PE_Scalar_State>`, a 2×2 adder tree), same
`cgra_run<...>` template GEMM uses, same C/C++-only tier (no SystemC).

See `../../Proyecto_C/sum_reduction_hls_c/README.md` for the full design
writeup and its comparison to
`../hls_vitis_sum_reduction_temporal_cgra_c` (the 1-cell temporal
counterpart).

**Status**: validated end-to-end with Vitis HLS 2024.2 —
`csim_design`/`csynth_design`/`cosim_design`/`export_design` all pass.
Real RTL cosim latency for the `start=true` run: **154 clock cycles**
(`sum_reduction_spatial_cgra_c_prj/solution1/sim/report/verilog/result.transaction.rpt`)
— ~9% *slower* than the temporal design despite needing only 6 `cgra_run`
FSM iterations vs. 27, and at ~3–4× the LUT/FF/DSP. See the README above for
the full explanation (short version: Vitis HLS schedules this design's
per-cell `pe_scalar_step` at ~23–24 cycles/iteration vs. ~4–5 for the
temporal design's `pe_mac_step`, which swamps the FSM-iteration-count
advantage at this small problem size).

## How to run

```bash
source /path/to/Xilinx/Vitis_HLS/2024.1/settings64.sh
cd Proyecto_HLS/hls_vitis_sum_reduction_spatial_cgra_c
vitis-run --mode hls --tcl run_hls.tcl
```

Runs `csim_design -> csynth_design -> cosim_design -> export_design`. Reports
land in `sum_reduction_spatial_cgra_c_prj/solution1/syn/report/csynth.rpt`
(and per-module `.rpt` files next to it, same layout as
`Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c/`).

## Files

- `run_hls.tcl` — automation script (top, part, clock, flow).
- `sum_reduction_spatial_hls_top_c.cpp` — minimal translation unit, only
  includes the real design
  (`../../Proyecto_C/sum_reduction_hls_c/SumReduction_Spatial_Top_C.cpp`).
- (no local testbench) — `run_hls.tcl` reuses
  `../../Proyecto_C/sum_reduction_hls_c/SumReduction_Spatial_Top_C__TB.cpp`
  directly, so both mapping styles are validated against the exact same 2
  input vectors.
