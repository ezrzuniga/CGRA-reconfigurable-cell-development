# cgra_final_noc/ — CGRA 3x3 final, interconexión Network-on-Chip

Versión Network-on-Chip (NoC) de [`cgra_final/`](../cgra_final/): **mismo** layout
3×3, **mismas** 5 celdas sintetizables (Memoria, Vectorial, Escalar, Routing,
MAC) en las mismas 9 posiciones — nada en `pe_hls/`, `memory_hls/`,
`mesh_hls/` ni `cgra_final/` se modifica. Lo único que cambia es **cómo se
comunican las celdas entre sí**: en vez de wires punto a punto fijos
(`mesh_hls/CGRA_Mesh_Static.h`), esta versión interconecta las celdas con una
malla de **routers** que reenvían **paquetes** (header con destino + payload),
decidiendo la ruta dinámicamente en cada ciclo en vez de usar un mux
preconfigurado.

## Qué es un NoC (y qué NO es, en este repo)

> Un NoC modela la comunicación interna del chip como una red de paquetes con
> routers y enlaces. Es una solución altamente escalable y eficiente para
> sistemas complejos con múltiples núcleos y aceleradores, ampliamente
> utilizada en SoCs modernos y arquitecturas de inteligencia artificial.

Este repo **ya tenía** algo parecido a un router: `pe_hls/routing/Routing_Cell.h`
es un switch-box de 8 puertos (4 de malla + 4 locales) — pero es
**circuit-switched**: el mux de cada salida lo fija una configuración
precargada por el host (`config_bank`/`ctx_sel`), y el dato mismo no lleva
ninguna dirección. Un NoC de verdad es **packet-switched**: el dato viaja con
su propio destino encima (`NoC_Packet`), y cada router decide hacia dónde
reenviarlo mirando ese header, no una configuración externa. `NoC_Router.h`
reusa exactamente la misma forma de 8 puertos que `Routing_Cell` (para poder
sentarse en el mismo lugar del diseño) pero reemplaza el mux estático por esa
decisión dinámica.

## Mapa de archivos

| Archivo | Qué es |
|---|---|
| `NoC_Packet.h` | El "flit": header (`dest_row`,`dest_col`,`valid`) + payload (`Link`, el mismo `PE_VectorData` de siempre). |
| `NoC_Router.h` | El router: 4 puertos de malla + 4 locales, ruteo **XY** (columna primero, fila después) y arbitraje de prioridad fija (tránsito > inyección local). Puramente combinacional, igual que `Routing_Cell`. |
| `NoC_Mesh_Static.h` | Genérico `ROWS×COLS×CellTs...`, mismo API que `mesh_hls/CGRA_Mesh_Static.h` (`load_instr`/`clear_instr`/`cell<I>()`/`trace()`); reusa su `CellChain`/`Getter` para el almacenamiento heterogéneo. Instancia 1 router por celda y hace el puente `Link`↔`Packet` en los bordes externos. |
| `CGRA_Final_NoC_Mesh.h` | Instancia concreta 3×3 (mismo layout que `cgra_final/CGRA_Final_Mesh.h`) sobre `NoC_Mesh_Static`. |
| `CGRA_Final_NoC_Mesh__TB.cpp` | Smoke test estructural — **mismos** vectores de prueba que `cgra_final/CGRA_Final_Mesh__TB.cpp`. |
| `CGRA_Final_NoC_GEMM__TB.cpp` | Puerto directo de `cgra_final_TB/CGRA_Final_GEMM__TB.cpp` — mismo programa espacial GEMM 2×2, sin ajustar ni un ciclo. |
| `NoC_Router__TB.cpp` | Prueba de la fábrica de routers en aislamiento: multi-hop real (2+ saltos, con doblez de columna a fila) y arbitraje bajo contienda — tráfico que las celdas reales del repo nunca generan por sí solas (ver más abajo). |

## La garantía central: equivalencia ciclo a ciclo con la malla directa

`NoC_Router::route()` es 100% combinacional (sin flanco de reloj propio,
misma disciplina que `Routing_Cell`) — un paquete inyectado llega a su vecino
en el mismo flanco de `clk` en que llegaría por un wire directo. Por eso
`CGRA_Final_NoC_Mesh__TB.cpp` reusa los vectores de prueba de
`CGRA_Final_Mesh__TB.cpp` sin cambiar un solo margen de ciclos, y
`CGRA_Final_NoC_GEMM__TB.cpp` reusa el programa espacial de
`CGRA_Final_GEMM__TB.cpp` (afinado ciclo a ciclo para la malla de wires
directos) **sin tocarlo** — ambos pasan con resultados idénticos. Esa es la
prueba más fuerte de que el router es un reemplazo de interconexión
"drop-in": mismo comportamiento observable, mecanismo interno distinto.

## Por qué hace falta una prueba aparte solo para el router (`NoC_Router__TB.cpp`)

El ISA de este repo (`pe_hls/pe_isa.h`: `SRC_/DST_NORTH|SOUTH|EAST|WEST`) solo
permite que una celda hable con su vecino **inmediato** — nunca hay una
dirección de "2 saltos" en una instrucción. Como consecuencia, todo el
tráfico que generan las 5 celdas reales (y por lo tanto las pruebas de arriba)
es **siempre de 1 solo salto**: se entrega en el primer router al que llega,
nunca viaja en tránsito por un segundo router. Eso demuestra equivalencia con
la malla directa, pero no ejercita lo que hace a un NoC un NoC de verdad
(enrutamiento dinámico multi-hop, arbitraje bajo contienda). `NoC_Router__TB.cpp`
inyecta paquetes sintéticos directo en los puertos de malla (con un destino
a 3 saltos de distancia) para probar ambas cosas de forma aislada y explícita.

## Limitaciones conocidas (documentadas, no escondidas)

- **Sin buffers ni backpressure**: si dos paquetes contienden por la misma
  salida, el de menor prioridad se descarta ese ciclo (`Packet()`,
  `valid=false`) — no hay FIFOs de entrada ni credit-based flow control.
  Con el tráfico real de este repo (siempre 0 ó 1 salto) esto **nunca**
  ocurre — ver la prueba de arbitraje en `NoC_Router__TB.cpp` para el
  escenario donde sí se fuerza a propósito.
- **Router combinacional, no pipelineado**: un paquete puede atravesar varios
  routers dentro del mismo ciclo de reloj (igual que varias `Routing_Cell`
  encadenadas hoy). Un NoC de silicio real normalmente pipelinea cada salto
  con al menos 1 registro por timing closure; acá se prioriza la
  equivalencia cycle-accurate con el resto del repo (ver sección anterior)
  sobre modelar esa latencia.
- **Direccionamiento de borde fijo, no una tabla de ruteo programable**: un
  dato que entra por un puerto externo siempre se etiqueta con el destino
  = el propio router de entrada (igual que hoy, un wire externo siempre cae
  directo en la celda de esa posición) — cualquier relevo posterior lo sigue
  haciendo la celda de computo con su lógica existente (p. ej. `Routing_Cell`
  reenviando el operando oeste hacia el bloque MAC), la fábrica NoC no
  necesita saber nada de esa decisión de aplicación.

## Compilar y ejecutar

Igual que el resto de `Proyecto_SystemC/` (ver `../README.md`):

```bash
export SYSTEMC_HOME=/ruta/a/tu/instalacion/systemc
cd Proyecto_SystemC/build   # o mkdir -p build && cd build && cmake ..
make -j4 CGRA_Final_NoC_Mesh__TB CGRA_Final_NoC_GEMM__TB NoC_Router__TB
./cgra_final_noc/CGRA_Final_NoC_Mesh__TB
./cgra_final_noc/CGRA_Final_NoC_GEMM__TB
./cgra_final_noc/NoC_Router__TB
```
