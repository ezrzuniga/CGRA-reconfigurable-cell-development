# gemm_temporal_hls_c — temporal counterpart to gemm_hls_c

`gemm_hls_c/GEMM_2x2_Mesh_C.h` maps 2×2 matrix multiplication **spatially**:
4 `PE_MAC` cells in a 2×2 mesh, one cell per output element `C[i][j]`, all 4
computed in parallel inside a single `start=true` invocation (2 phases for
`k=0,1`, `NUM_PHASES=2`, `INSTR_MEM_SIZE=4`).

This folder is the **temporal** counterpart: a single `PE_MAC` cell
(`GEMM_2x2_Temporal_Mesh_C.h`, `CGRA_Mesh_Static_C<1,1,32,1,PE_MAC_State>`),
reused **4 separate times** — once per output element. Each invocation runs
its own 2-phase `k=0,1` accumulation (`acc += A[i][k]*B[k][j]`, same `OP_MAC`
contract as the spatial design's cells) and exports one `C[i][j]` on
`out_E`. Because `cgra_run` calls `mesh_clear_acc()` at the start of every
`start=true` invocation (`cgra_hls_c/CGRA_Top_C.h`), each of the 4 runs
starts from a clean accumulator with no extra reset instruction needed — the
host just calls the top 4 times with different `A`/`B` operands and collects
4 results.

Same defensive pattern as
`sum_reduction_hls_c/SumReduction_Temporal_Mesh_C.h`: the `MAC` instruction
lives at **slot 2**, not slot 0, with slots 0–1 as `NOP` — `cgra_run`'s
`for(;;)` loop unconditionally re-executes slot 0 then slot 1 twice more
after each run finishes (the "phantom wrap-around", see that file's header
comment for the full explanation), and a self-accumulating `MAC` at slot 0
would double-count. This lesson was already paid for once by the sum
reduction design and is reapplied here without rediscovering it.

Both designs use the **same 2 test cases** as `GEMM_2x2_HLS_Top_C__TB.cpp`
(`A=[[1,2],[3,4]], B=[[5,6],[7,8]] → C=[[19,22],[43,50]]` and
`A=[[-3,5],[2,-4]], B=[[6,-1],[-2,3]] → C=[[-28,18],[20,-14]]`), so results
and, once run through Vitis HLS, cycle counts are directly comparable.

## Real measured results (Vitis HLS 2024.2)

Both designs went through the complete real flow —
`csim_design → csynth_design → cosim_design → export_design` — with
`cosim_design` (actual RTL simulation) reporting `PASS` for both.

| Metric | Temporal (1 cell) | Spatial (4 cells, `gemm_hls_c`) |
|---|---:|---:|
| Cycles per `start=true` invocation | 51 (produces 1 output) | 82 (produces all 4 outputs) |
| **Invocations needed for a full 2×2 matrix** | **4** | **1** |
| **Total cycles for a full matrix** | **204** | **82** |
| Clock achieved (post-synthesis) | 2.891 ns | 2.891 ns |
| LUT (post-synthesis) | 1199 | 4467 |
| FF (post-synthesis) | 1551 | 7098 |
| DSP | 3 | 12 |

**Unlike the sum-reduction comparison, here spatial genuinely wins on
latency**: one shot of the 4-cell design finishes a whole matrix in 82
cycles, vs. 204 cycles for 4 sequential shots of the 1-cell design — a real
**2.49× speedup** — at a real **3.7× LUT / 4.6× FF / 4× DSP** area cost. This
is the "textbook" spatial-parallelism tradeoff (genuine latency win, real
area price), in contrast to sum reduction's counterintuitive result where
the spatial design was both slower *and* more expensive. The difference:
both GEMM designs use the *same* cell type (`PE_MAC`), so there's no
per-cell HLS-scheduling asymmetry like the one that sank spatial sum
reduction (`PE_Scalar` vs. `PE_MAC` synthesized very differently there) —
see `../../comparison_matrix_multiplication.md` (repo root, one level up)
for the full data analysis and the side-by-side contrast with sum
reduction's result.

## Files

- `GEMM_2x2_Temporal_Mesh_C.h` — 1-cell mesh + program (MAC at slot 2).
- `GEMM_2x2_Temporal_Top_C.h`/`.cpp` — thin `cgra_run<...>` wrapper, same
  pattern as `GEMM_2x2_HLS_Top_C.h`/`.cpp`.
- `GEMM_2x2_Temporal_Top_C__TB.cpp` — programs once, then calls the top 4×
  per test case (one per output element), same 2 cases as the spatial
  design's testbench.

## Running it

```bash
source /path/to/Xilinx/Vitis_HLS/2024.2/settings64.sh

# g++ standalone (fast, no Vitis synthesis):
g++ -std=c++17 -I"$XILINX_HLS/include" \
    GEMM_2x2_Temporal_Top_C.cpp GEMM_2x2_Temporal_Top_C__TB.cpp \
    -o gemm_temporal_test && ./gemm_temporal_test

# Real Vitis HLS flow:
cd ../../Proyecto_HLS/hls_vitis_gemm_2x2_temporal_cgra_c
vitis-run --mode hls --tcl run_hls.tcl
```
