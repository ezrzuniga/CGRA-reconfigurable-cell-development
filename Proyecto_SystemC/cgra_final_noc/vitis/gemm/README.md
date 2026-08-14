# vitis/gemm — GEMM 2×2 sobre la CGRA NoC real

Ver `../README.md` para requisitos, cómo correr, y la explicación completa
(por qué se sintetiza la malla real en vez de un kernel aritmético, los 2
bugs de síntesis corregidos en `Proyecto_C/`, formato de los reportes).

Top: `GEMM_NoC_HLS_Top_C(A[2][2], B[2][2], C[2][2])` — programa el bloque MAC
sistolico 2×2 embebido en la malla 3×3 una única vez (`static` persistente),
después cada llamada arma el caso, alimenta A/B en 2 pasadas (k=0/k=1) y lee
C de los bordes reales de la malla.

## Resultado (csynth_design)

| Latencia (ciclos) | Reloj estimado | LUT | FF | DSP |
|---|---|---|---|---|
| 58 – 236 | 4.473 ns (223.59 MHz) | 161196 / 117120 (138%) | 35553 / 234240 (15%) | 189 / 1248 (15%) |

`csim_design` (mismo binario que g++, corrido antes vía
`../../../../Proyecto_C/cgra_final_noc_c/GEMM_NoC_HLS_Top_C__TB.cpp`): PASS
en los 3 casos (matrices aleatorias, semilla `20260810`).
