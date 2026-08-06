# sum_reduction_hls_c — spatial vs. temporal mapping, same problem

Two independent implementations of the same 8-element sum reduction
(`total = Σ v[0..7]`), built on the exact same reusable pieces the rest of
the C/HLS tier uses (`cgra_hls_c/CGRA_Top_C.h`'s `cgra_run<...>` template,
`mesh_hls_c/CGRA_Mesh_Static_C.h`), so that the *only* thing that differs
between them is the mapping style. Companion Vitis HLS projects:
`Proyecto_HLS/hls_vitis_sum_reduction_temporal_cgra_c/` and
`Proyecto_HLS/hls_vitis_sum_reduction_spatial_cgra_c/`.

| | Temporal | Spatial |
|---|---|---|
| File | `SumReduction_Temporal_{Mesh,Top}_C.h/.cpp` | `SumReduction_Spatial_{Mesh,Top}_C.h/.cpp` |
| Mesh | `CGRA_Mesh_Static_C<1,1,32,1, PE_MAC_State>` — 1 cell | `CGRA_Mesh_Static_C<2,2,32,1, PE_Scalar_State x4>` — 4 cells |
| `NUM_PHASES` | 8 (1 external value presented per phase) | 1 (all 8 values presented at once) |
| `INSTR_MEM_SIZE` | 3 (2 NOP + 1 MAC, see below) | 3 (one level of the reduction tree per slot) |
| Strategy | One cell, reused every phase: `acc += v[phase]` (`OP_MAC`) | 4 cells, one 3-level adder tree computed once |
| `cgra_run` FSM iterations (C-model) | 27 | 6 |
| **Real RTL cosim latency** (Vitis HLS 2024.2, measured) | **141 cycles** | **154 cycles** |
| Real post-synthesis area (Vivado) | 1205 LUT / 1551 FF / 3 DSP / 0 SRL | 3857 LUT / 5230 FF / 12 DSP / 20 SRL |

Both designs have been run through the complete real flow
(`csim_design → csynth_design → cosim_design → export_design`) with Vitis
HLS 2024.2 — see "Real measured results" below for the full numbers and, more
interestingly, *why* the mapping with fewer FSM iterations isn't the faster
one once actually synthesized.

## The temporal mapping

`SumReduction_Temporal_Mesh_C.h` — a single `PE_MAC` cell. `cgra_run` is
driven with `NUM_PHASES=8`: each phase presents one new vector element on
`in_N[phase][0]`, and the resident instruction does
`acc += in_N * 1` (`OP_MAC`, `src_b=SRC_IMM=1`), exporting the running total
to `out_E` every cycle — the same contract as
`../../Proyecto_SystemC/pe/mac/PE_MAC_SumReduction__TB.cpp`, just driven by
`cgra_run`'s generic phase engine instead of a hand-written SystemC loop.

**Why the MAC instruction lives at slot 2, not slot 0** (the non-obvious bug
this design caught before being confirmed correct): `cgra_run`'s `for(;;)`
loop (`cgra_hls_c/CGRA_Top_C.h`) calls `mesh_step()` *unconditionally* every
iteration, including the two iterations spent in `ST_WAIT_DONE`/`ST_DONE`
after the real run finishes. At that point every cell's `pc` has already
wrapped back to 0, so those two "phantom" cycles silently re-execute slot 0
then slot 1 — using the *last phase's* still-held external edges. With
`INSTR_MEM_SIZE=1` the only slot **is** the MAC instruction, so those 2
phantom cycles double-add the last vector element (verified with a Python
re-trace of the exact FSM: total came out as 28 instead of 22 for
`{6,-2,9,4,0,7,-5,3}`). GEMM never hits this because its risky instruction
(`MAC ... ->ACC`) sits mid-program and the *export* (`MOV ACC->port`) is the
very last slot, so the phantom slot-0/1 re-execution never touches the port
that gets read. Same fix applied here: slots 0–1 are `NOP` (harmless if
repeated), the accumulate lives at slot 2, safely out of the phantom
cycles' reach.

## The spatial mapping

`SumReduction_Spatial_Mesh_C.h` — 4 `PE_Scalar` cells in the same 2×2
physical layout GEMM uses. Because every cell of a 2×2 mesh sits on *two*
mesh boundaries at once (both its row and its column are edge rows/columns),
each cell exposes exactly 2 external ports — 8 external ports across the
mesh, one per input element, all presented in a single phase:

```
P00.N=v0  P00.W=v1        P01.N=v2  P01.E=v3
P10.S=v4  P10.W=v5        P11.S=v6  P11.E=v7
```

A 3-level adder tree runs across 3 instruction slots (1 cycle per level,
since compute-and-relay are fused into single instructions, e.g.
`ADD(N,W)->EAST` both computes and forwards a partial sum in the same
cycle):

- **Level 1** (slot 0, 1 cycle, 4 cells in parallel): `p0=v0+v1` (P00),
  `p1=v2+v3` (P01), `p2=v4+v5` (P10), `p3=v6+v7` (P11).
- **Level 2** (slot 1, 1 cycle, 2 cells in parallel): `rowSum_top=p0+p1`
  (P01, relayed south to P11), `rowSum_bottom=p2+p3` (P11).
- **Level 3** (slot 2, 1 cycle): `total = rowSum_bottom + rowSum_top` (P11),
  exported on P11's external east port.

Unlike the temporal design, this one needed no defensive NOP padding — every
instruction recomputes its result from fresh operands (register or relayed
neighbor value) rather than accumulating into its own past output, so the
phantom slot-0/1 re-execution just recomputes the same values from the same
still-held inputs and is naturally idempotent.

## Correctness: caught with a Python re-trace before ever compiling

Before either design touched a compiler, its exact cycle-by-cycle behavior —
including the phantom-cycle bug described above — was verified by
re-implementing the precise semantics of
`pe_mac_step`/`pe_scalar_step`/`mesh_step`/`cgra_run`'s FSM in a standalone
Python script and running both designs against 2 test vectors:

| Vector | Expected total | Temporal result | Spatial result |
|---|---:|---:|---:|
| `{6,-2,9,4,0,7,-5,3}` | 22 | 22 ✓ | 22 ✓ |
| `{100,-55,30,7,-12,4,-9,15}` | 80 | 80 ✓ | 80 ✓ |

Both then compiled and ran correctly with real g++ + `ap_int.h`
(`SumReduction_Temporal_Top_C__TB.cpp` / `SumReduction_Spatial_Top_C__TB.cpp`,
both `PASS`), and both went through the complete real Vitis HLS 2024.2 flow —
`csim_design → csynth_design → cosim_design → export_design` — with
`cosim_design` (actual RTL simulation, not an estimate) reporting
`PASS` for both.

One real, pre-existing bug this surfaced along the way: `PE_MAC_HLS_C.h`'s
`select_src`/`writeback` were templated on only 3 of `PE_MAC_State`'s 4
template parameters (missing `INSTR_MEM_SIZE`), silently relying on it
defaulting to `4`. It only ever worked because GEMM happens to use
`INSTR_MEM_SIZE=4` (the default) — this temporal design's `INSTR_MEM_SIZE=3`
broke template deduction outright (compile error, not a silent bug). Fixed
in `PE_MAC_HLS_C.h` to match the already-correct 4-parameter pattern used in
`PE_Scalar_HLS_C.h`; GEMM re-verified with no behavior change afterward.

## Real measured results — and a genuine surprise

| Metric | Temporal (1 cell) | Spatial (4 cells) |
|---|---:|---:|
| `cgra_run` FSM iterations (idealized C model) | 27 | 6 |
| **RTL cosim latency, real `start=true` run** | **141 cycles** | **154 cycles** |
| Clock achieved (post-synthesis) | 2.891 ns | 2.891 ns |
| LUT (post-synthesis) | 1205 (1%) | 3857 (3%) |
| FF (post-synthesis) | 1551 (~1%) | 5230 (2%) |
| DSP | 3 | 12 |
| SRL | 0 | 20 |

(Cosim numbers are per-transaction cycle counts pulled directly from
`sim/report/verilog/result.transaction.rpt` — the `start=true` transaction is
unambiguous in both traces since every other transaction is a cheap
`prog_valid` poke: 12 cycles for temporal, 14 for spatial.)

**The counterintuitive result: at n=8, the spatial mapping is not faster —
it's ~9% slower in real RTL, while costing ~3.2× the LUT, ~3.4× the FF, and
4× the DSP.** This inverts the naive "spatial has fewer mesh cycles, so it
must be faster" intuition (6 vs. 27 in the idealized C-level FSM model), and
the `csynth.rpt` "Performance & Resource Estimates" table explains why: a
single synthesized `pe_mac_step` costs ~4–5 cycles/iteration (matching
GEMM's own `PE_MAC` figures almost exactly), but a single synthesized
`pe_scalar_step` in the spatial design costs **23–24 cycles/iteration** —
roughly 5× more expensive *per mesh step* despite doing strictly less
arithmetic (`ADD` only, no multiply). With only 6 FSM iterations to amortize
that per-step cost over, the spatial design's fewer-but-far-costlier steps
end up net slower than the temporal design's more-numerous-but-cheap ones.
Exactly why Vitis HLS schedules the 2×2 `PE_Scalar` mesh's per-cell step so
much less efficiently than GEMM's 2×2 `PE_MAC` mesh (same cell count, same
mesh shape) wasn't root-caused further here — a legitimate next question if
this comparison gets extended — but the practical lesson stands: **the
"spatial should win" intuition from the idealized cycle-count model does not
automatically survive contact with real HLS scheduling, especially at small
problem sizes where per-invocation/per-step overhead dominates the raw
compute-cycle savings.** A larger reduction (n=64, n=256, deeper tree) would
be a natural follow-up to see whether spatial eventually overtakes temporal
once the O(log n) vs. O(n) FSM-iteration gap grows large enough to outweigh
the per-step overhead penalty.

## Reports and reproducing this

```bash
source /path/to/Xilinx/Vitis_HLS/2024.2/settings64.sh

cd Proyecto_HLS/hls_vitis_sum_reduction_temporal_cgra_c
vitis-run --mode hls --tcl run_hls.tcl
# synthesis report: sum_reduction_temporal_cgra_c_prj/solution1/syn/report/SumReduction_Temporal_Top_C_csynth.rpt
# real RTL cycle counts: sum_reduction_temporal_cgra_c_prj/solution1/sim/report/verilog/result.transaction.rpt

cd ../hls_vitis_sum_reduction_spatial_cgra_c
vitis-run --mode hls --tcl run_hls.tcl
# synthesis report: sum_reduction_spatial_cgra_c_prj/solution1/syn/report/SumReduction_Spatial_Top_C_csynth.rpt
# real RTL cycle counts: sum_reduction_spatial_cgra_c_prj/solution1/sim/report/verilog/result.transaction.rpt
```

The generated project directories (`*_prj/`) are build output, same as
`Proyecto_HLS/hls_vitis_gemm_2x2_cgra_c/gemm_2x2_cgra_c_prj/` — not meant to
be hand-edited, only regenerated by `run_hls.tcl`.

## g++ standalone validation (before touching Vitis)

Same 2-stage validation discipline as the rest of the repo
(`new_CGRA_guide.md` section 7): compile and run each testbench with g++
against `ap_int.h` from a local Vitis HLS install before running real HLS.
`ap_int.h` lives under `$XILINX_HLS/include` once `settings64.sh` is
sourced (not under `Vitis_HLS/<ver>/include` directly):

```bash
source /path/to/Xilinx/Vitis_HLS/2024.2/settings64.sh

g++ -std=c++17 -I"$XILINX_HLS/include" \
    SumReduction_Temporal_Top_C.cpp SumReduction_Temporal_Top_C__TB.cpp \
    -o sumred_temporal_test && ./sumred_temporal_test

g++ -std=c++17 -I"$XILINX_HLS/include" \
    SumReduction_Spatial_Top_C.cpp SumReduction_Spatial_Top_C__TB.cpp \
    -o sumred_spatial_test && ./sumred_spatial_test
```

Both testbenches print `PASS`/`FAIL` per case and a final summary, same
format as `GEMM_2x2_HLS_Top_C__TB.cpp` — and, as of this writing, both
actually pass, on real g++ and real Vitis HLS 2024.2 (`csim`/`csynth`/
`cosim`/`export` all green), not just in the Python re-trace above.
