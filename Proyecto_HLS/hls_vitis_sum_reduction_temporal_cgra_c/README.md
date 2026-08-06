# hls_vitis_sum_reduction_temporal_cgra_c

Vitis HLS project for `SumReduction_Temporal_Top_C`
(`../../Proyecto_C/sum_reduction_hls_c/SumReduction_Temporal_Top_C.h`) — the
**temporal** mapping of sum reduction: a real synthesizable CGRA mesh
(`CGRA_Mesh_Static_C<1,1,32,1, PE_MAC_State>`, a *single* cell), same
`cgra_run<...>` template GEMM uses, same C/C++-only tier (no SystemC).

See `../../Proyecto_C/sum_reduction_hls_c/README.md` for the full design
writeup, including why this needs 3 instruction slots per phase (not 1) and
how it compares to `../hls_vitis_sum_reduction_spatial_cgra_c` (the 4-cell
spatial counterpart).

**Status**: validated end-to-end with Vitis HLS 2024.2 —
`csim_design`/`csynth_design`/`cosim_design`/`export_design` all pass.
Real RTL cosim latency for the `start=true` run: **141 clock cycles**
(`sum_reduction_temporal_cgra_c_prj/solution1/sim/report/verilog/result.transaction.rpt`).
Post-synthesis area: 1205 LUT / 1551 FF / 3 DSP / 0 SRL. See the README above
for the full comparison against the spatial design, including a genuinely
counterintuitive result (spatial ends up *slower* at this problem size).

## How to run

```bash
source /path/to/Xilinx/Vitis_HLS/2024.1/settings64.sh
cd Proyecto_HLS/hls_vitis_sum_reduction_temporal_cgra_c
vitis-run --mode hls --tcl run_hls.tcl
```

Runs `csim_design -> csynth_design -> cosim_design -> export_design`. Reports
land in `sum_reduction_temporal_cgra_c_prj/solution1/syn/report/csynth.rpt`
(and per-module `.rpt` files next to it, same layout as
`Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c/`).

## Files

- `run_hls.tcl` — automation script (top, part, clock, flow).
- `sum_reduction_temporal_hls_top_c.cpp` — minimal translation unit, only
  includes the real design
  (`../../Proyecto_C/sum_reduction_hls_c/SumReduction_Temporal_Top_C.cpp`).
- (no local testbench) — `run_hls.tcl` reuses
  `../../Proyecto_C/sum_reduction_hls_c/SumReduction_Temporal_Top_C__TB.cpp`
  directly, so both mapping styles are validated against the exact same 2
  input vectors.
