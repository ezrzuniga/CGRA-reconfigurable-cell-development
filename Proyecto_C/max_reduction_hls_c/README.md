# max_reduction_hls_c — temporal vs. spatial, no native MAX opcode

Temporal vs. spatial mapping of an 8-element maximum reduction. This
project's ISA has no `MAX`/select instruction, so every combine is built
from `max(a,b) = a + (b-a)·[a<b]` (`SUB`, `SLT`, `MUL`, `ADD` chained) —
making this the most arithmetic-heavy comparison in the survey.

- **Temporal** (`MaxReduction_Temporal_*`): 1 `PE_Scalar` cell, 8 phases.
  `slot0/1=NOP`, the 4-instruction combine + export live at slot≥2 (same
  phantom-cycle discipline as `sum_reduction_hls_c`).
- **Spatial** (`MaxReduction_Spatial_*`): 4 `PE_Scalar` cells in the same
  2×2 layout as `sum_reduction_hls_c`'s spatial design, 3-level tree,
  `INSTR_MEM_SIZE=20` (each combine is 4–5 instructions, plus margin NOPs
  between tree levels). This one's cross-cell timing was intricate enough
  that it was verified with a Python re-trace of `cgra_run`'s exact FSM
  *before* any C++ was written — see the header comment in
  `MaxReduction_Spatial_Mesh_C.h`.

## Real measured results (Vitis HLS 2024.2)

Both designs passed the complete real flow (`csim → csynth → cosim →
export`, `cosim_design` = actual RTL simulation).

| | Temporal (1 cell) | Spatial (4 cells, tree) |
|---|---:|---:|
| Real RTL cycles | **302** | **579** |
| LUT / FF / DSP (post-synthesis) | 1162 / 1331 / 3 | 4400 / 4014 / 12 |

Spatial loses decisively (1.92× slower, 3–4× area) — worse than sum
reduction's already-negative result, because the tree concentrates *three*
4-instruction combines into one invocation instead of amortizing the same
total combine count across many cheap phases. Full analysis:
`../../comparison_max_reduction.md` (repo root).

## Files

- `MaxReduction_Temporal_{Mesh,Top}_C.h/.cpp`, `MaxReduction_Temporal_Top_C__TB.cpp`
- `MaxReduction_Spatial_{Mesh,Top}_C.h/.cpp`, `MaxReduction_Spatial_Top_C__TB.cpp`

Vitis HLS projects: `../../Proyecto_HLS/hls_vitis_max_reduction_temporal_cgra_c/`,
`../../Proyecto_HLS/hls_vitis_max_reduction_spatial_cgra_c/`.
