# sum_reduction16_hls_c — does spatial catch up at a larger n?

Direct follow-up to the open question in `sum_reduction_hls_c/README.md`:
does the spatial mapping's iteration-count advantage eventually outweigh its
per-invocation overhead once the problem is bigger? Same 2×2 mesh, same cell
types as the n=8 design, only the input size changes: 8 → 16 elements.

- **Temporal** (`SumReduction16_Temporal_*`): the n=8 design unchanged
  except `NUM_PHASES: 8 → 16` — scaling temporal is a 1-constant change.
- **Spatial** (`SumReduction16_Spatial_*`): a 2×2 mesh only has 8
  simultaneous external ports, so 16 elements don't fit in one phase on 4
  cells. This needed a genuine **3-stage redesign**: reset `reg0` (stage 0)
  → each cell accumulates its own 4 elements over 2 phases (stage 1) →
  reprogram and combine the 4 partial sums with a 2-level tree (stage 2,
  skips the old level-1 since the leaves are already summed). Reprogramming
  between stages doesn't touch `reg_file`, so stage 1's partial sums
  survive into stage 2.

**Real bug this caught**: the mesh is `static` and persists across separate
test-case runs; `PE_Scalar` has no automatic register-clear equivalent to
`PE_MAC`'s `mesh_clear_acc()`. Without the explicit stage-0 reset, a second
test case silently accumulated on top of the first case's leftover `reg0`.
Fixed by adding the reset stage — see `SumReduction16_Spatial_Mesh_C.h`.

## Real measured results (Vitis HLS 2024.2) — the answer is no

Both designs passed the complete real flow (`csim → csynth → cosim →
export`).

| | n=8 (baseline) | n=16 (this design) |
|---|---:|---:|
| Temporal cycles | 141 | 261 |
| Spatial cycles | 154 | **951** (3 invocations: 260+431+260) |
| Spatial/temporal ratio | 1.09× slower | **3.64× slower** |

Spatial falls **further** behind, not closer — going from 1 invocation
(n=8) to 3 invocations (n=16, because the mesh's fixed port count forced a
multi-stage design) means paying `cgra_run`'s large per-invocation overhead
3× instead of once, which swamps any benefit from needing fewer tree levels.
LUT cost also jumped from 3.2× to **12.0×**. Full analysis:
`../../comparison_sum_reduction_n16.md` (repo root).

## Files

- `SumReduction16_Temporal_{Mesh,Top}_C.h/.cpp`, `SumReduction16_Temporal_Top_C__TB.cpp`
- `SumReduction16_Spatial_{Mesh,Top}_C.h/.cpp`, `SumReduction16_Spatial_Top_C__TB.cpp`

Vitis HLS projects: `../../Proyecto_HLS/hls_vitis_sum_reduction16_temporal_cgra_c/`,
`../../Proyecto_HLS/hls_vitis_sum_reduction16_spatial_cgra_c/`.
