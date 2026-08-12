# CGRA_Final_GEMM__TB

GEMM 2×2 mapped onto the bottom-right 2×2 block of the 3×3 `CGRA_Final_Mesh`
(`cgra_final/CGRA_Final_Mesh.h`). Two matrices `A`, `B` are streamed in over
two feed steps (`k=0`, `k=1`); each MAC cell accumulates one element of
`C = A×B` internally and emits it once both steps have run.

## Mesh layout and roles

```
        col 0             col 1              col 2
row 0  Memoria(0,0)     Vectorial(0,1)     Escalar(0,2)
       idle              relay B col0→S     relay B col1→S
                          (1 cyc, registered) (1 cyc, registered)

row 1  Routing(1,0)     MAC P00(1,1)       MAC P01(1,2)
       ctx0: real W→E    A,B → C[0][0]      A,B → C[0][1]
       ctx1: E→real W    (no real edge)     out_E[1] (real edge)

row 2  Routing(2,0)     MAC P10(2,1)       MAC P11(2,2)
       ctx0: real W→E    A,B → C[1][0]      A,B → C[1][1]
                          out_S[1] (real edge) out_E[2] (real edge)
```

Inputs: `A` row 0 via `in_W[1]`, `A` row 1 via `in_W[2]`, `B` col 0 via
`in_N[1]` (through Vectorial), `B` col 1 via `in_N[2]` (through Escalar).
`Memoria(0,0)` never participates — no GEMM this size needs SRAM.

## Instruction schedule (repeats every 8 addresses, once per `k` step)

| addr | P00 (1,1)                          | P01 (1,2)                    | P10 (2,1)                    | P11 (2,2)              |
|-----:|------------------------------------|-------------------------------|-------------------------------|--------------------------|
| 0    | —                                  | —                              | —                              | —                        |
| 1    | `A → east` (relay to P01)          | —                              | `A → east` (relay to P11)      | —                        |
| 2    | `B → south` (relay to P10)         | `B → south` (relay to P11)     | —                              | —                        |
| 3    | —                                  | —                              | —                              | —                        |
| 4    | `MAC(W,N) → ACC`                   | `MAC(W,N) → ACC`               | `MAC(W,N) → ACC`               | —                        |
| 5    | —                                  | —                              | —                              | `MAC(W,N) → ACC`         |
| 6    | `ACC → west` (Routing ctx1 → `out_W[1]`) | `ACC → east` (→ `out_E[1]`) | `ACC → south` (→ `out_S[1]`)   | —                        |
| 7    | —                                  | —                              | —                              | `ACC → east` (→ `out_E[2]`) |

Relay addresses (1, 2) start one cycle later than a naive "0-cycle" reading
would suggest — see *Timing notes* below.

## Results

Seed `20260810`, 3 randomized cases, `A`, `B` ∈ [-9, 9]:

| Case | A               | B               | C expected           | C obtained            | Result |
|-----:|-----------------|-----------------|-----------------------|-------------------------|:------:|
| 1    | `[[-6,-2],[-6,-7]]` | `[[0,2],[-1,9]]` | `[[2,-30],[7,-75]]`   | `[[2,-30],[7,-75]]`     | PASS   |
| 2    | `[[-3,-9],[3,-7]]`  | `[[1,-7],[-1,-4]]` | `[[6,57],[10,7]]`   | `[[6,57],[10,7]]`       | PASS   |
| 3    | `[[4,1],[-6,-1]]`   | `[[-4,6],[-1,2]]`  | `[[-17,26],[25,-38]]`| `[[-17,26],[25,-38]]`   | PASS   |

Exit code `0`, all 3 cases PASS.

## Timing notes (found while debugging this testbench)

- **Boundary-write settle time.** A value the testbench writes to a
  boundary port (`in_W`/`in_N`) between two `advance_cycles()` calls isn't
  visible to a downstream reader until *one cycle later* than a "0-cycle
  relay" would suggest — `sc_signal::write()` made outside a clocked process
  only applies at the next `sc_start()`'s update phase. Every relay that
  depends on a freshly written boundary value is scheduled one address later
  than the naive reading to account for this (confirmed by instrumenting
  `PE_MAC_HLS::tick()` directly).
- **Routing-context reload needs the same margin.** Reloading
  `Routing_Cell`'s output context (`ctx1`) right before reading through it
  isn't safe on the *first* use — it only worked in later test cases because
  they reused an already-settled context from a previous run. Loading it two
  cycles before the read fixes it for every case, including the first.
- **Leftover debug code was the original bug.** Before either timing issue
  applied, `P10`'s real program had been overwritten with a "PC counter"
  instruction left over from a prior debugging session, which broke the
  systolic block entirely (`C[1][0]` and `C[1][1]` came out as garbage or
  zero). Restoring its real relay/MAC/write-back sequence was the first fix.
