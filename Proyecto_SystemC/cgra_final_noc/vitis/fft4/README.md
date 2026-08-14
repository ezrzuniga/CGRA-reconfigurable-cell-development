# vitis/fft4 — FFT de 4 puntos sobre la CGRA NoC real

Ver `../README.md` para requisitos, cómo correr, y la explicación completa.

Top: `FFT4_NoC_HLS_Top_C(x0..x3 re/im, X0..X3 re/im*)` — mariposas radix-2
(DIF) sobre el bloque MAC, sin `OP_MUL`: los twiddles `W4^k` (siempre ±1 o
±j) se resuelven con el empaquetado de lanes de la entrada, no con
multiplicación real (ver comentario de cabecera del `.h` en
`../../../../Proyecto_C/cgra_final_noc_c/FFT4_NoC_HLS_Top_C.h`).

## Resultado (csynth_design)

| Latencia (ciclos) | Reloj estimado | LUT | FF | DSP |
|---|---|---|---|---|
| 62 – 171 | 4.473 ns (223.59 MHz) | 133375 / 117120 (114%) | 32414 / 234240 (14%) | 144 / 1248 (12%) |

`csim_design`: PASS en los 3 casos (impulso, escalón, complejo aleatorio —
semilla `20260810`), validado contra una DFT de 4 puntos de fuerza bruta
independiente.
