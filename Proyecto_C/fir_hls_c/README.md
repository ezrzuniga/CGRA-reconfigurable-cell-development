# fir_hls_c — temporal vs. spatial 3-tap FIR filter

Two mappings of a 3-tap FIR filter, `y[n] = w0·x[n] + w1·x[n+1] + w2·x[n+2]`,
4 output samples — same `cgra_run<...>` template, same `PE_MAC` cell type on
both sides (unlike `sum_reduction_hls_c`, which mixes `PE_MAC` and
`PE_Scalar`), so this comparison isolates the mapping style from any
per-cell-type synthesis variance.

- **Temporal** (`FIR_Temporal_*`): 1 `PE_MAC` cell (`CGRA_Mesh_Static_C<1,1,...>`),
  invoked 4 times (once per output sample), same `slot0/1=NOP, slot2=MAC`
  defensive pattern as `sum_reduction_hls_c`/`gemm_temporal_hls_c`.
- **Spatial** (`FIR_Spatial_*`): 4 `PE_MAC` cells in a 1×4 row
  (`CGRA_Mesh_Static_C<1,4,...>`). Because `ROWS=1`, every cell's N and S
  ports are simultaneously external — the 4 outputs are fully independent,
  needing **no relay wiring at all**, the simplest spatial topology in this
  whole project.

## Real measured results (Vitis HLS 2024.2)

Both designs passed the complete real flow (`csim → csynth → cosim →
export`, `cosim_design` = actual RTL simulation).

| | Temporal (1 cell ×4) | Spatial (4 cells ×1) |
|---|---:|---:|
| Total real RTL cycles for all 4 outputs | **264** | **321** |
| LUT / FF / DSP (post-synthesis) | 1199 / 1551 / 3 | 7345 / 9515 / 12 |

Spatial is **slower** here (1.22×) despite zero relay overhead, at **6.1×**
the area — a third, distinct data point alongside GEMM (spatial wins) and
sum reduction (spatial loses less badly). Full analysis:
`../../comparison_fir_convolution.md` (repo root).

## Files

- `FIR_Temporal_{Mesh,Top}_C.h/.cpp`, `FIR_Temporal_Top_C__TB.cpp`
- `FIR_Spatial_{Mesh,Top}_C.h/.cpp`, `FIR_Spatial_Top_C__TB.cpp`

Vitis HLS projects: `../../Proyecto_HLS/hls_vitis_fir_temporal_cgra_c/`,
`../../Proyecto_HLS/hls_vitis_fir_spatial_cgra_c/`.
