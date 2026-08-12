# CGRA_Final_Softmax4__TB

A 4-element softmax mapped onto the same 3×3 `CGRA_Final_Mesh`. The ISA has
no exponential and no divide (`pe_isa.h` only has ADD/SUB/AND/OR/XOR/shifts/
compares/MUL/MAC), so this splits the same way a real accelerator would:
the mesh computes the parallelizable part — `2ˣ` per element and the
tree-sum of those values — and a single scalar division happens once, off
the array, in the C++ testbench.

**Base 2, not base e, and exact rather than approximate.** `2ˣ = e^(x·ln2)`,
so base-2 softmax is standard softmax evaluated at logits pre-scaled by
`ln(2)` — a different temperature, not a different function. And because
`2ˣ` for an integer `x` is computed as a bit-shift (`1 << (x+9)`, `OP_SLL`)
rather than a truncated Taylor series, there's no approximation error at
all — every value the mesh produces matches an independent reference
bit-for-bit.

## Mesh layout and roles

```
        col 0             col 1                col 2
row 0  Memoria(0,0)     Vectorial(0,1)       Escalar(0,2)
       idle              relay x1→S           relay x3→S
                                               (plain scalar — fine here,
                                                no complex data involved)

row 1  Routing(1,0)     MAC P00(1,1) — hub   MAC P01(1,2)
       ctx0: real W→E    e0 = 1<<(x0+9)        e3 = 1<<(x3+9)
       ctx1: E→real W    e1 = 1<<(x1+9)        → west, to P00
                          SUM = e0+e1+e2+e3
                          → Routing → out_W[1]

row 2  Routing(2,0)     MAC P10(2,1)         MAC P11(2,2)
       ctx0: real W→E    e2 = 1<<(x2+9)        idle — P00 is already a
                          → north, to P00        direct neighbor of both
                                                  P10 and P01, so no third
                                                  combiner cell is needed
```

Inputs: `x0` via `in_W[1]`, `x1` via `in_N[1]` (through Vectorial), `x2` via
`in_W[2]`, `x3` via `in_N[2]` (through Escalar) — one value each, no
time-multiplexing needed (unlike `SumReduce8`'s 8 inputs over 4 ports).
`Memoria(0,0)` and `P11` are idle.

## Instruction schedule (single pass, addresses 0–11)

| addr | P00 (1,1)                          | P01 (1,2)                    | P10 (2,1)              |
|-----:|--------------------------------------|--------------------------------|---------------------------|
| 1    | `tmp0 = x0+9`                        | —                               | `tmp2 = x2+9`             |
| 2    | `e0 = 1<<tmp0`                       | `tmp3 = x3+9`                   | `e2 = 1<<tmp2`            |
| 3    | `tmp1 = x1+9`                        | `e3 = 1<<tmp3`                  | —                          |
| 4    | `e1 = 1<<tmp1`                       | —                               | —                          |
| 5    | `reg2 = e0+e1`                       | `e3 → west` (to P00)            | `e2 → north` (to P00)     |
| 7    | `reg2 += e2` (south)                 | —                               | —                          |
| 8    | `reg2 += e3` (east) = **SUM**        | —                               | —                          |
| 9    | *(settle; load Routing ctx1 here)*   | —                               | —                          |
| 10   | `SUM → west` (Routing → `out_W[1]`)  | —                               | —                          |
| 11   | `e0 → west` (spot-check, same port)  | —                               | —                          |

Every relay wire carries exactly one value, one time — the same "no
wire-reuse hazard" discipline `SumReduce8` established, not FFT4's tighter
(and initially buggy) reuse.

## Results

Independently cross-checked against an `int64_t` reference (`1LL <<
(x[i]+9)`, summed) — not the mesh's own arithmetic restated:

| Case | x0..x3            | SUM expected | SUM obtained | e0 spot-check | Result |
|-----:|--------------------|:------------:|:------------:|:---------------:|:------:|
| 1    | `0, 0, 0, 0`        | 2048         | 2048         | 512 = 512        | PASS   |
| 2    | `0, 0, 6, 0`        | 34304        | 34304        | 512 = 512        | PASS   |
| 3    | `-6, -2, -6, -7`    | 148          | 148          | 8 = 8            | PASS   |

Exit code `0`, all 3 cases PASS — on the first build, no debugging
iteration needed.

### Case 2's softmax output (division done in the testbench)

| x_i | 2^x_i | softmax₂ |
|----:|------:|---------:|
| 0   | 512   | 0.014925 |
| 0   | 512   | 0.014925 |
| 6   | 32768 | 0.955224 |
| 0   | 512   | 0.014925 |

## Design notes

- **Why base 2 instead of base e.** Computing `eˣ` needs either a
  polynomial approximation (which diverges or loses positivity/monotonicity
  outside a narrow input range on this integer ALU) or a lookup table — and
  this ISA has no register-indexed addressing (`reg_a`/`reg_b` are static
  fields set at load time, not computed at runtime), so a true runtime LUT
  isn't expressible at all. `2ˣ` sidesteps both problems: it's a real
  hardware technique (many NPU softmax units compute `2^(x·log₂e)` for
  exactly this reason) and it's exact for integer `x`, not approximate.
- **Why the division stays off the array.** It's needed exactly once per
  output vector, not once per element — parallelizing a single scalar
  operation buys nothing, so splitting it out (mesh computes the reduction,
  host does the one divide) matches how real accelerators are actually built.
- **Escalar is fine here.** Unlike `FFT4`, nothing routed through Escalar is
  complex-valued — its lane-0-only scalar bridge (see `FFT4`'s notes) isn't
  a problem when there's no imaginary part to lose.
