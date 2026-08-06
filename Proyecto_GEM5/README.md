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

## CGRA-offload workloads (`*_cgra_gem5_workload.c`)

Counterparts to the plain-CPU workloads above that actually run their
kernel on the CGRA, from inside gem5, instead of comparing against numbers
measured by a separate toolchain. All 6 done end-to-end — see each
`comparison_gem5_cgra_*.md` (repo root) for the individual writeup.

| File | Kernel | CGRA cycle constant source | `atomic` | `o3` |
|---|---|---|---|---|
| `vector_add4_cgra_gem5_workload.c` | vector_add4 spatial (SIMD) | `comparison_vector_add.md` §2.1 (33 cyc) | clean | clean |
| `sum_reduction8_cgra_gem5_workload.c` | sum_reduction8 temporal + spatial (both) | `comparison_sum_reduction.md` §2.1 (141 / 154 cyc) | clean | clean |
| `sum_reduction16_cgra_gem5_workload.c` | sum_reduction16 temporal + spatial (both, spatial = 3 stages) | `comparison_sum_reduction_n16.md` §3.1 (261 / 951 cyc) | clean | clean |
| `max_reduction8_cgra_gem5_workload.c` | max_reduction8 temporal + spatial (both) | `comparison_max_reduction.md` §3.1 (302 / 579 cyc) | clean | clean |
| `fir3tap_cgra_gem5_workload.c` | fir3tap temporal + spatial (both) | `comparison_fir_convolution.md` §2.1 (264 / 321 cyc) | clean | clean |
| `matmul2x2_cgra_gem5_workload.c` | matmul2x2 temporal + spatial (both) | `comparison_matrix_multiplication.md` §2.1 (204 / 82 cyc) | clean | **crashes — see below** |

**`o3` note on `matmul2x2`**: this one workload hits a reproducible
pre-existing gem5 O3 CPU-model bug (`src/cpu/o3/inst_queue.cc:1523:
panic: Dependency graph N ... not empty!`), isolated to GEMM's spatial
design specifically (the largest single-phase instruction-memory program
of any spatial design here) called repeatedly — not a CGRA-bridge
correctness bug. See `comparison_gem5_cgra_matmul2x2.md` §2 for the full
bisection. Use `--cpu atomic` for this one kernel; the other 5 are clean
under both models.

**How it works**: gem5 SE mode has no OS/page-table layer, so a compiled
user binary can't reach a real memory-mapped device the way FS-mode Linux
could. Instead, each `_cgra` workload calls a new gem5 pseudo-instruction,
`m5_cgra_run(kernel_id, &args)` — the same mechanism `m5ops` already uses
for `m5_exit`/`m5_dumpstats` (a trapped magic ARM64 instruction, decoded
directly by gem5, no address-space plumbing needed). gem5's C++ handler
(`gem5/src/cgra/cgra_kernels.{hh,cc}`, dispatched from a new case in
`gem5/src/sim/pseudo_inst.{hh,cc}`) runs the kernel's **real**
Vitis-HLS-synthesizable C++ model from `Proyecto_C/<kernel>_hls_c/` — the
exact source that was actually synthesized/cosim'd for this repo's
`comparison_*.md` docs, compiled here as ordinary host C++ (no HLS
toolchain needed at gem5-build time; it only needs Xilinx's `ap_int.h`,
available locally at `/home/rex/tools/Xilinx/Vitis/2024.2/include`) — for
the correct output values, then stalls the calling CPU thread for that
kernel's real cosim-measured cycle count (a per-kernel constant, not
re-derived inside gem5) before returning control.

### Building a `_cgra` workload

```bash
# 1. Assemble gem5's m5ops client stub for aarch64 (one-time; also
#    auto-generates m5_cgra_run since it's added to M5OP_FOREACH)
mkdir -p m5ops_arm64
aarch64-linux-gnu-gcc -c -I/home/rex/gem5/include \
    /home/rex/gem5/util/m5/src/abi/arm64/m5op.S -o m5ops_arm64/m5op.o

# 2. Cross-compile and link the workload against it
aarch64-linux-gnu-gcc -O0 -static -o /tmp/vector_add4_cgra_arm \
    vector_add4_cgra_gem5_workload.c m5ops_arm64/m5op.o

# 3. Run under gem5 SE mode exactly like the plain-CPU workloads
/home/rex/gem5/build/ARM/gem5.opt --outdir=/tmp/m5out_cgra \
    gem5_se_config.py --binary /tmp/vector_add4_cgra_arm --isa arm --cpu o3
grep -E "numCycles|numInsts" /tmp/m5out_cgra/stats.txt
```

If `gem5/src/cgra/cgra_kernels.cc` or `gem5/src/sim/pseudo_inst.{hh,cc}`
change, rebuild gem5 first: `cd /home/rex/gem5 && scons build/ARM/gem5.opt
-j8`.
