# CGRA_Final_SumReduce8__TB

An 8-input sum reduction (`TOTAL = v0+v1+…+v7`) as a 3-level binary tree over
the same 3×3 `CGRA_Final_Mesh`. Unlike the GEMM (2 active MAC cells) and FFT4
(2 active + 2 pure-relay) examples, this is the one where all 4 MAC cells
actually compute a sum. With only 4 real edges for 8 inputs, each
direct-input cell **time-multiplexes** its port (value 1, then value 2) and
**accumulates in a register**, combining that temporal technique with the
spatial tree-combine GEMM and FFT4 already used.

## Mesh layout and roles

```
        col 0             col 1                col 2
row 0  Memoria(0,0)     Vectorial(0,1)       Escalar(0,2)
       idle              relay v4, v5→S       relay v6, v7→S
                                               (scalar-only is fine here:
                                                no imaginary part to lose)

row 1  Routing(1,0)     MAC P00(1,1) — hub   MAC P01(1,2)
       ctx0: real W→E    reg0=v0+v1 (west)    reg0=v6+v7 (north)
       ctx1: E→real W    reg1=v4+v5 (north)   →south, then forwards
                          reg2=branchA          branchB west→P00
                          TOTAL=reg2+branchB
                          → Routing → out_W[1]

row 2  Routing(2,0)     MAC P10(2,1)         MAC P11(2,2)
       ctx0: real W→E    reg0=v2+v3 (west)    reg0 = west+north
                          → east, to P11         = branchB
                                                → north, to P01
```

Inputs: `v0,v1` via `in_W[1]`, `v2,v3` via `in_W[2]`, `v4,v5` via `in_N[1]`
(through Vectorial), `v6,v7` via `in_N[2]` (through Escalar) — each port
carries two values, one after another. `Memoria(0,0)` is the only idle cell.

## Instruction schedule (single pass, addresses 0–13)

| addr | P00 (1,1)              | P01 (1,2)             | P10 (2,1)         | P11 (2,2)         |
|-----:|-------------------------|-------------------------|---------------------|---------------------|
| 1    | `reg0 = v0`              | —                       | `reg0 = v2`         | —                   |
| 2    | `reg1 = v4`              | `reg0 = v6`             | —                    | —                   |
|      | *(testbench rewrites all 4 ports: t1 → t2)* |             |            |                     |
| 4    | `reg0 += v1`             | —                       | `reg0 += v3`        | —                   |
| 5    | `reg1 += v5`             | `reg0 += v7`            | —                    | —                   |
| 6    | `reg2 = reg0+reg1` (branchA) | `reg0 → south` (to P11) | `reg0 → east` (to P11) | — |
| 8    | —                        | —                       | —                    | `reg0 = west+north` (branchB) |
| 9    | —                        | —                       | —                    | `reg0 → north` (to P01) |
| 11   | —                        | `south → west` (forwards branchB to P00) | — | — |
| 12   | *(settle; load Routing ctx1 here)* | —            | —                    | —                   |
| 13   | `TOTAL = reg2 + east → west` (Routing → `out_W[1]`) | — | —          | —                   |

Every relay wire here carries exactly one value, one time — unlike FFT4's
`P00 → P01` wire, which had to carry two different values at two different
times. That was FFT4's actual bug; this design avoids the hazard by
construction rather than by adding margin.

## Results

| Case | v0..v7                            | TOTAL expected | TOTAL obtained | Result |
|-----:|-------------------------------------|:--------------:|:--------------:|:------:|
| 1    | `1, 1, 1, 1, 1, 1, 1, 1`             | 8               | 8               | PASS   |
| 2    | `1, 2, 4, 8, 16, 32, 64, 128`        | 255             | 255             | PASS   |
| 3    | `-3, 1, 17, 20, 2, 0, 13, 7`         | 57              | 57              | PASS   |

Exit code `0`, all 3 cases PASS — on the first build, with no debugging
iteration needed, by applying the timing rules learned from GEMM and FFT4 up
front:

- give a freshly written boundary port one settle cycle before relying on it,
- give a freshly reloaded Routing output context two settle cycles before
  reading through it,
- never schedule a relay wire's second use before its first value has
  actually been consumed.
