# vitis/sum_reduce8 — reducción de suma (8 enteros) sobre la CGRA NoC real

Ver `../README.md` para requisitos, cómo correr, y la explicación completa.

Top: `SumReduce8_NoC_HLS_Top_C(v[8], *total)` — arbol de reducción binario
de 3 niveles sobre las 4 celdas MAC de la malla (única aplicación de las 4
donde las 4 celdas MAC hacen trabajo real, no solo relevos).

## Resultado (csynth_design)

| Latencia (ciclos) | Reloj estimado | LUT | FF | DSP |
|---|---|---|---|---|
| 67 – 193 | 4.473 ns (223.59 MHz) | 186955 / 117120 (160%) | 37120 / 234240 (16%) | 192 / 1248 (15%) |

`csim_design`: PASS en los 3 casos (unos, potencias de dos, aleatorios con
signo — semilla `20260810`).
