# vector_add_hls_c — temporal (scalar loop) vs. spatial (SIMD lanes)

The one comparison in this survey where "spatial" means **SIMD lanes inside
a single cell**, not more physical cells in the mesh: `C[n] = A[n] + B[n]`
for a 4-element vector.

- **Temporal** (`VectorAdd_Temporal_*`): 1 `PE_Scalar` cell, `VLEN=1`,
  invoked 4 times (once per element). `ADD` recomputes fresh from external
  operands every call — no self-reference, so unlike the MAC-based designs
  in this repo it's naturally safe against `cgra_run`'s phantom wrap-around
  cycles and needs no defensive NOP padding (`INSTR_MEM_SIZE=1`).
- **Spatial** (`VectorAdd_Spatial_*`): 1 `PE_Vector` cell, `VLEN=4`, 1
  invocation — the 4 additions happen in the same ALU cycle, one per SIMD
  lane. `ROWS=COLS=1` on **both** sides; only `VLEN` differs.

## Real measured results (Vitis HLS 2024.2)

Both designs passed the complete real flow (`csim → csynth → cosim →
export`, `cosim_design` = actual RTL simulation).

| | Temporal (VLEN=1 ×4) | Spatial (VLEN=4 ×1) |
|---|---:|---:|
| Total real RTL cycles for all 4 elements | **92** | **33** |
| LUT / FF / DSP (post-synthesis) | 1056 / 1137 / 3 | 3190 / 5003 / 12 |

Spatial wins clearly here — **2.79× faster**, and because the win is large
the area-delay product comes out almost even (unlike every mesh-cell-count
comparison in this survey, which favored temporal by 1.5–7×). Full
analysis: `../../comparison_vector_add.md` (repo root).

## Files

- `VectorAdd_Temporal_{Mesh,Top}_C.h/.cpp`, `VectorAdd_Temporal_Top_C__TB.cpp`
- `VectorAdd_Spatial_{Mesh,Top}_C.h/.cpp`, `VectorAdd_Spatial_Top_C__TB.cpp`

Vitis HLS projects: `../../Proyecto_HLS/hls_vitis_vector_add_temporal_cgra_c/`,
`../../Proyecto_HLS/hls_vitis_vector_add_spatial_cgra_c/`.
