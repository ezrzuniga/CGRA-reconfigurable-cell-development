# Proyecto_GEM5 — CPU baseline for the temporal/spatial CGRA survey

6 small C workloads, each computing the exact same algorithm (same test
vectors) as one of the CGRA temporal/spatial comparisons in
`Proyecto_C/*_hls_c/`, run under [gem5](https://www.gem5.org/) in
syscall-emulation (SE) mode to give a **conventional-CPU reference point**
alongside the CGRA cycle counts. Full analysis:
`../comparison_cpu_baseline.md` (repo root) — **read that document's
methodology section before quoting any absolute cycle number from here**:
these are whole-program cycles (dominated by ~93k instructions of C-runtime
startup), not kernel-isolated, so they are not directly comparable in
magnitude to the CGRA measurements without further `m5ops` instrumentation.

## Workloads

| File | Mirrors | Test vectors |
|---|---|---|
| `sum_reduction_workload.c` | (original, 7-element + seed) | `{6,-2,9,4,0,7,-5,3}` seed=100 etc. |
| `sum_reduction8_gem5_workload.c` | `Proyecto_C/sum_reduction_hls_c/` | same 2 vectors as the CGRA n=8 design |
| `sum_reduction16_gem5_workload.c` | `Proyecto_C/sum_reduction16_hls_c/` | same 2 vectors as the CGRA n=16 design |
| `matmul2x2_gem5_workload.c` | `Proyecto_C/gemm_hls_c/`, `gemm_temporal_hls_c/` | same 2×2 A/B cases |
| `fir3tap_gem5_workload.c` | `Proyecto_C/fir_hls_c/` | same 3-tap weights/samples |
| `vector_add4_gem5_workload.c` | `Proyecto_C/vector_add_hls_c/` | same 4-element a/b |
| `max_reduction8_gem5_workload.c` | `Proyecto_C/max_reduction_hls_c/` | same 2 vectors as the CGRA max design |

All compiled `-O0` on purpose — an optimizing compiler can trivially
collapse these tiny fixed-size loops to a precomputed constant, which would
make gem5 measure "return a constant" instead of "run the kernel."

## How to reproduce

Requires a gem5 build with the ARM ISA and an `aarch64-linux-gnu-gcc` cross
compiler (this environment has both: `/home/rex/gem5/build/ARM/gem5.opt`,
`aarch64-linux-gnu-gcc`).

```bash
# 1. Cross-compile a workload
aarch64-linux-gnu-gcc -O0 -static -o /tmp/sum_reduction8_arm \
    sum_reduction8_gem5_workload.c

# 2. Run under gem5 SE mode (choose --cpu atomic|timing|minor|o3)
/home/rex/gem5/build/ARM/gem5.opt --outdir=/tmp/m5out \
    gem5_se_config.py --binary /tmp/sum_reduction8_arm --isa arm --cpu o3

# 3. Cycle/instruction counts land in:
grep -E "numCycles|numInsts" /tmp/m5out/stats.txt
```

`--isa arm` was added to `gem5_se_config.py` for this environment (it
previously only listed `riscv`/`x86` — this machine has an ARM gem5 build
and cross-compiler available, not RISC-V or a working X86 flow at the time
this was added).

## `gem5_results/`

Saved `stats.txt` output from the runs `comparison_cpu_baseline.md` reports
on (`<workload>_<cpu>_stats.txt`, `atomic` and `o3` for all 6 workloads) —
kept for provenance, same spirit as the Vitis HLS `*_prj/` build outputs
elsewhere in this repo.
