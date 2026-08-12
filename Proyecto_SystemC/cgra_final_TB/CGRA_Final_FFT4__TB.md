# CGRA_Final_FFT4__TB

A 4-point complex FFT (radix-2, decimation-in-frequency) mapped onto the same
3×3 `CGRA_Final_Mesh`, reusing the MAC cells as a plain ADD/SUB butterfly
engine instead of MAC/ACC. N=4 is the one transform size where every twiddle
factor is ±1 or ±j — pure sign/lane manipulation, never a real
multiplication — so no `OP_MUL` is needed and the whole thing fits this
integer, non-fixed-point ISA cleanly.

## Mesh layout and roles

```
        col 0             col 1              col 2
row 0  Memoria(0,0)     Vectorial(0,1)     Escalar(0,2)
       idle              relay x1→S         idle — see note below
                          (preserves all     (collapses Link to a
                           4 lanes)           scalar; unusable here)

row 1  Routing(1,0)     MAC P00(1,1)       MAC P01(1,2)
       ctx0: real W→E    a = x0+x2          c = x1+x3
       ctx1: E→real W    b = x0-x2          d = twiddle(x1-x3)
                          X0, X2 → Routing   X1, X3 → out_E[1] (real edge)

row 2  Routing(2,0)     MAC P10(2,1)       MAC P11(2,2)
       ctx0: real W→E    relay only:        relay only:
                          x2→P00, x3→P11    x3→P01
```

**Why Escalar is unused:** `PE_Scalar_Cell_HLS` bridges the mesh's 4-lane
`Link` to the inner scalar cell's single `sc_int` by reading only lane 0 on
the way in and broadcasting one value to all 4 lanes on the way out — fine
for a real scalar (like GEMM's B operand), but it silently discards the
imaginary component of any complex number routed through it. `x3` instead
travels `P10 → P11 → P01`, sharing `in_W[2]` with `x2` by time-multiplexing
the same port.

**The twiddle trick:** the ISA's ALU applies one opcode uniformly across all
4 lanes — there's no lane-shuffle instruction to swap real/imaginary. The
testbench instead arranges each input's own 4 lanes so a *single* `SUB`
produces both the plain difference (lanes 0–1, unused) and the
already-twiddled value (lanes 2–3, exactly what's needed for `×(-j)`) for
free:

| input | lane pattern       | why |
|-------|--------------------|-----|
| x0, x2 | `[re, im, re, im]` | no twiddle needed — simple duplicate |
| x1, x3 | `[re, im, im, -re]` | `SUB` of two such vectors leaves `-j·(x1-x3)` sitting in lanes 2–3 |

## Instruction schedule (single pass, addresses 0–13, no repeating loop)

| addr | P00 (1,1)                       | P01 (1,2)                    | P10 (2,1)             | P11 (2,2)          |
|-----:|----------------------------------|-------------------------------|-------------------------|----------------------|
| 1    | —                                 | —                              | `x2 → north` (to P00)  | —                    |
| 2    | `x1 → east` (to P01)              | —                              | —                       | —                    |
|      |                                   | *(testbench rewrites `in_W[2]`: x2 → x3)* |               |                      |
| 3    | —                                 | —                              | `x3 → east` (to P11)   | —                    |
| 4    | `a = x0+x2 → reg0`                | —                              | —                       | —                    |
| 5    | `b = x0-x2 → reg1`                | —                              | —                       | `x3 → north` (to P01) |
| 8    | —                                 | `c = x1+x3 → reg0`             | —                       | —                    |
| 9    | —                                 | `d = twiddle(x1-x3) → reg1`    | —                       | —                    |
| 10   | `b → east` (to P01)               | `c → west` (to P00)            | —                       | —                    |
| 11   | *(settle; load Routing ctx1 here)* | —                              | —                       | —                    |
| 12   | `X0 = a+c → west` (Routing → `out_W[1]`) | `X1 = b+d → east` (→ `out_E[1]`, lanes 2–3) | — | — |
| 13   | `X2 = a-c → west`                 | `X3 = b-d → east` (lanes 2–3)  | —                       | —                    |

Note addresses 2 and 10 reuse the **same physical wire** (`P00 → P01`): `x1`
travels it at address 2, and `b` isn't sent over it until address 10 — after
P01 has already consumed `x1` for `c`/`d` at addresses 8–9. Scheduling that
relay any earlier silently clobbers `x1` before it's used; this was the
actual bug hit while building this program.

## Results

Independently cross-checked against a brute-force DFT (`Σ xₙ·W₄ᵏⁿ`), not the
mesh's own `a/b/c/d` equations:

| Case | x0     | x1     | x2    | x3     | X0 expected | X1 expected | X2 expected | X3 expected | Obtained | Result |
|-----:|--------|--------|-------|--------|-------------|-------------|-------------|-------------|----------|:------:|
| 1    | 0+0j   | 1+0j   | 0+0j  | 0+0j   | 1+0j        | 0-1j        | -1+0j       | 0+1j        | matches  | PASS   |
| 2    | 1+0j   | 1+0j   | 0+0j  | 0+0j   | 2+0j        | 1-1j        | 0+0j        | 1+1j        | matches  | PASS   |
| 3    | -6-2j  | -6-7j  | 0+2j  | -1+9j  | -13+2j      | -22+1j      | 1-2j        | 10-9j       | matches  | PASS   |

Exit code `0`, all 3 cases PASS.

## Timing notes (in addition to the GEMM ones, which still apply)

- **Escalar cannot carry complex data.** Discovered by instrumenting
  `PE_MAC_HLS::tick()` and seeing `x3`'s imaginary part silently become `0`
  (or a broadcast of its real part) after passing through Escalar — traced
  to `PE_Scalar_Cell_HLS::bridge_in_N()`/`bridge_out_N()`, which are lane-0
  only by construction, not a bug in this testbench's logic.
- **Wire-reuse ordering.** Reusing a relay wire for two different values at
  two different times (as address 2 and address 10 do here on `P00→P01`) is
  safe only if the *consumer* of the first value runs before the *producer*
  sends the second. Getting this backwards was the concrete regression that
  had to be found and fixed while building this schedule.
