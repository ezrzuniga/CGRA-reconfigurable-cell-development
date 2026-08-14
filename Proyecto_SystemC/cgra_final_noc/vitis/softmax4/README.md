# vitis/softmax4 — softmax base-2 (4 elementos) sobre la CGRA NoC real

Ver `../README.md` para requisitos, cómo correr, y la explicación completa.

Top: `Softmax4_NoC_HLS_Top_C(x0..x3, *sum_out, *e0_out)` — la malla computa
`EXP2(x)=1<<(x+9)` (exacto vía shift, sin punto flotante) para las 4
entradas y la reducción `SUM=sum(EXP2(x_i))`; la división final
`softmax_i = EXP2(x_i)/SUM` es la parte escalar que un acelerador real deja
fuera del array, para el host — por eso el top expone `SUM` y `e0`
(spot-check), no el vector softmax completo, mismo contrato que el
testbench de referencia.

## Resultado (csynth_design)

| Latencia (ciclos) | Reloj estimado | LUT | FF | DSP |
|---|---|---|---|---|
| 53 – 165 | 4.473 ns (223.59 MHz) | 125489 / 117120 (107%) | 25412 / 234240 (11%) | 96 / 1248 (8%) |

`csim_design`: PASS en los 3 casos (logits iguales, un logit dominante,
aleatorios con signo — semilla `20260810`).
