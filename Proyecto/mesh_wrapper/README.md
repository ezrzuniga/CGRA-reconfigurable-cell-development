# mesh_wrapper

Puente TLM-2.0 <-> señales planas sobre una `CGRA_Mesh_Heterogeneous<2,2>` real, con
el mismo layout del diagrama de nivel 2 (`Entrega_Avance_2/images/lvl2_diagram.png`):

```
(0,0) Enrutamiento   (0,1) Memoria
(1,0) Escalar        (1,1) Vectorial
```

Expone un único `tlm_utils::simple_target_socket` con el mismo mapa de registros que
`riscv_dma_main_mem_components/csr_dma.cpp` ya asume del lado `cgra_socket`, y
traduce cada transacción en `mesh.load_instr(...)`/pokes sobre los puertos de borde
del mesh real (`mesh/CGRA_Mesh_Heterogeneous.h`) y, para `PROGRAM_FULL_PIPELINE`, en
la misma secuencia de programación de Enrutamiento/Memoria ya validada en
`mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp`.

**Alcance de esta carpeta**: standalone. No se instancia `CSR_DMA`/`RiscvCore`/
`MainMemory` reales — el testbench trae su propio módulo de test (`FakeCsrDma`) que
imita el protocolo. La integración de verdad (con `CSR_DMA`/`RiscvCore`/`MainMemory`
reales) vive en `../riscv_dma_main_mem_components/` (`RiscvDmaSystem__TB`), que
instancia este mismo `MeshWrapper` sin modificarlo.

## Requisitos
- SystemC con TLM-2.0 (variable de entorno `SYSTEMC_HOME`, si no está en una ruta
  estándar del sistema). Misma instalación que ya usa
  `riscv_dma_main_mem_components/` — los headers `tlm.h`/`tlm_utils/` vienen incluidos
  ahí, no hace falta nada adicional.
- CMake >= 3.16 y un compilador con soporte C++17.
- (Opcional) [GTKWave](https://gtkwave.sourceforge.net/) para ver el waveform generado.

## Compilar
Desde la raíz del repo (el build está unificado, ver `CMakeLists.txt` raíz):
```
mkdir -p build && cd build
cmake ..
make
```
El binario queda en `build/mesh_wrapper/`.

## Ejecutar
```
./MeshWrapper_CSRDMA_Sim__TB     # PROGRAM_VECTOR_ADD + PROGRAM_FULL_PIPELINE
./MeshWrapper_SumReduction__TB   # PROGRAM_SUM_REDUCTION (reducción por suma)
```
Ambos generan un `.vcd` en el directorio desde el que se ejecutan, con las señales
de control/borde del mesh interno más el estado de la PE (`wrapper.trace(tf)`
reenvía a `mesh_.trace(tf)`). Se puede inspeccionar con
`gtkwave mesh_wrapper_csr_dma_sim_wave.vcd` /
`gtkwave mesh_wrapper_sum_reduction_wave.vcd`.

### Por qué una instrucción nueva por elemento (no un puerto que cambia con la instrucción fija)

`run_sum_reduction_dataflow` (`mesh_wrapper.cpp`) NO deja una única instrucción
`reg0 = reg0 + oeste` residente en Escalar y va cambiando el dato en su borde oeste
cada ciclo. Se probó esa forma primero y falló: `mesh.load_instr(...)` activa la
instrucción nueva en el MISMO flanco de `clk` en que se carga (no en el siguiente,
como sugeriría el patrón "load + wait + clear" que se usa en el resto de este
archivo para cargas *idempotentes*, ej. contextos de Enrutamiento/Memoria). Un
acumulador residente vuelve a sumar lo que haya en el puerto en **cada ciclo que
esa instrucción siga cargada** — cualquier margen extra "por seguridad" repite la
suma del último valor, y calcular el margen mínimo exacto para que cada elemento se
sume una sola vez es frágil y depende de detalles de scheduling de SystemC. La
solución robusta: cargar una instrucción **nueva** por cada elemento, con el valor
ya empaquetado en el campo `imm` (`reg0 = reg0 + imm(v[i])`), reusando el mismo
patrón de "load + exactamente 1 ciclo" que ya es seguro en todos lados porque cada
carga sustituye a la anterior antes de que alcance a re-ejecutarse.

## Mapa de registros

Mismo mapa que `CSR_DMA` ya asume del lado `cgra_socket` (ver comentario en
`riscv_dma_main_mem_components/csr_dma.cpp`), confirmado además por el código real de
`send_configuration()`/`send_input_data_to_cgra()`/`execute_cgra()`/
`receive_output_data_from_cgra()` en ese mismo archivo:

| Offset | Registro            | R/W | Tamaño   |
|--------|----------------------|-----|----------|
| `0x00` | `CONFIG`             | W   | 4 bytes  |
| `0x04` | `START`              | W   | 4 bytes  |
| `0x08` | `STATUS`             | R   | 4 bytes  |
| `0x0C` | `DONE`               | R   | 4 bytes  |
| `0x10` | `INPUT_DATA_BUFFER`  | W   | 32 bytes |
| `0x14` | `OUTPUT_DATA_BUFFER` | R   | 16 bytes |

**Nota**: `STATUS` (`0x08`) se implementa por completitud del mapa documentado, aunque
ningún `CSR_DMA` real lo lee hoy (`csr_dma.cpp` nunca hace un `TLM_READ_COMMAND` a esa
dirección).

## Catálogo de programas (`MeshProgram`)

`CONFIG` no es un bitstream arbitrario — es un índice a un catálogo mínimo de
programas pre-armados, definido en `mesh_wrapper.h`. El valor numérico de cada
entrada coincide a propósito con el `CGRA_KERNEL` homónimo de
`../riscv_dma_main_mem_components/cgra_kernel.h` — `CSR_DMA` reenvía `cgra_config`
tal cual como `CONFIG` (ver `csr_dma.cpp::send_configuration()`).

| Valor | Nombre                  | Qué hace | Celdas involucradas |
|-------|--------------------------|----------|----------------------|
| `0`   | `PROGRAM_VECTOR_ADD`     | `c = a + b`, elementwise real (4 lanes independientes) | Solo Vectorial (`(1,1)`), usando sus dos bordes reales (S=a, E=b) — Enrutamiento/Memoria/Escalar inactivas. Mismo resultado que la versión `<1,1>` original. |
| `3`   | `PROGRAM_FULL_PIPELINE`  | `e = a + b*2` | Las 4: `b` entra por el borde norte real de Enrutamiento, viaja por Memoria (round-trip NoC) y Enrutamiento otra vez hasta el borde norte (interno) de Escalar; Escalar calcula `b*2`; Vectorial suma su borde sur real (`a`) con `b*2` (borde oeste, interno) y expone el resultado en su borde este real. |
| `4`   | `PROGRAM_SUM_REDUCTION`  | Reducción por suma: `total = seed + sum(v[0..6])` | Las 4: `INPUT_DATA_BUFFER` (32 bytes) se reinterpreta como 8 enteros de 32 bits — `word[0]` es el seed (mismo camino Enrutamiento→Memoria→Enrutamiento→Escalar que "b" en `PROGRAM_FULL_PIPELINE`) y `word[1..7]` son los 7 elementos a sumar. Escalar acumula: primero siembra `reg0 = seed`, después carga una instrucción **nueva por cada elemento** (`reg0 = reg0 + imm(v[i])`, un ciclo cada una — ver "Por qué una instrucción por elemento" abajo) y por último reenvía `reg0` a Vectorial, que lo expone en su borde este real. |

Cualquier otro valor genera `SC_REPORT_ERROR` (la transacción sigue respondiendo
`TLM_OK_RESPONSE`, el catálogo simplemente no encontró un programa para ese índice).

**Por qué `a` y `b` no son intercambiables**: `PE_Scalar_Cell` y el puerto NoC de
`PE_Memory_Mesh_Cell` solo hablan lane 0 (broadcast, no elementwise real — ver los
comentarios de cabecera de `../pe/scalar/PE_Scalar_Cell.h` y
`../memory/PE_Memory_Mesh_Cell.h`). Por eso `a` (que puede variar libremente por
lane, ej. `{1,2,3,4}`) SIEMPRE llega directo a un borde real de Vectorial, sin pasar
por Escalar ni Memoria; `b` es el único operando que atraviesa Enrutamiento →
Memoria → Escalar en `PROGRAM_FULL_PIPELINE`, y por eso tiene que ser uniforme entre
lanes (un escalar de verdad, ej. `{1,1,1,1}`) para que el resultado sea correcto.

**Nota**: el mesh interno se instancia con `INSTR_MEM_SIZE=1` (no el default de 16).
El PC de cada PE es un contador libre que incrementa todos los ciclos habilitados
(no hay saltos/branch en el ISA), así que `pc % INSTR_MEM_SIZE` solo vale `0` de forma
permanente si `INSTR_MEM_SIZE=1` — con un valor mayor, el PC abandona la dirección
`0` (donde `load_instr` escribe) en el primer ciclo habilitado y nunca vuelve. Mismo
motivo por el que `mesh/CGRA_Mesh_SmokeTest__TB.cpp`/`ComplexTest__TB.cpp` usan
siempre `addr=0` con `INSTR_MEM_SIZE=1`.

Para agregar una entrada nueva:
1. Nuevo valor en el `enum MeshProgram` (`mesh_wrapper.h`), coincidiendo con el
   `CGRA_KERNEL` que le corresponda si `CSR_DMA` va a dispararlo.
2. Nueva rama en `MeshWrapper::handle_config_write` (programa Escalar/Vectorial) y,
   si necesita Enrutamiento/Memoria, en `MeshWrapper::handle_start_write` /
   `run_full_pipeline_dataflow` (`mesh_wrapper.cpp`).
3. Actualizar esta tabla.

## Protocolo de programación y control

Caso `PROGRAM_VECTOR_ADD` (solo Vectorial, el más simple):

```
FakeCsrDma (SC_THREAD "run")                  MeshWrapper::b_transport                 CGRA_Mesh_Heterogeneous<2,2> "mesh_"
--------------------------------              ---------------------------------------  ------------------------------------
1. TLM WRITE addr=0x00 (CONFIG=0)    ------->  handle_config_write(PROGRAM_VECTOR_ADD)
                                                 vector_instr = {ADD,S,E,DST_EAST}
                                                 mesh_.load_instr(1,1,0,vector_instr) -> instr_sig[3].write(...)  (poke, celda Vectorial)
                                                 wait(1 flanco clk)            --------> latchea instr_in
                                                 mesh_.clear_instr(1,1)
                                                 programmed_ = true
                                                 <--- TLM_OK_RESPONSE

2. TLM WRITE addr=0x10 (INPUT, 32B)   ------->  handle_input_write(data,32)
                                                 a = bytes[0:16), b = bytes[16:32)
                                                 in_S_[1].write(Link(a))       --------> mesh_.in_S[1] (borde real de Vectorial)
                                                 in_E_[1].write(Link(b))       --------> mesh_.in_E[1] (borde real de Vectorial)
                                                 <--- TLM_OK_RESPONSE

3. TLM WRITE addr=0x04 (START=1)      ------->  handle_start_write(1)
                                                 status_=1; done_=0
                                                 wait(2 flancos clk)           --------> Vectorial computa OP_ADD(in_S,in_E)->out_E
                                                 result_ = out_E_[1].read()    <-------- mesh_.out_E[1]
                                                 status_=0; done_=1
                                                 <--- TLM_OK_RESPONSE

4. TLM READ addr=0x0C (DONE), loop    ------->  *data = done_ (poll con wait(10,SC_NS)
   hasta leer 1                                 entre intentos, igual que
                                                 CSR_DMA::execute_cgra())

5. TLM READ addr=0x14 (OUTPUT, 16B)   ------->  handle_output_read(data,16)
                                                 memcpy(data, result_, 16)
                                                 <--- TLM_OK_RESPONSE
```

`PROGRAM_FULL_PIPELINE` sigue el mismo esqueleto de 5 pasos, pero el paso 3
(`handle_start_write`) despacha a `run_full_pipeline_dataflow()` en vez de esperar 2
flancos directo: esa función hace, en secuencia, exactamente las 4 fases ya probadas
en `mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp` (Enrutamiento ctx0 relay →
Memoria ctx0 NoC→SRAM → Memoria ctx1 SRAM→NoC → Enrutamiento ctx1 relay →
Escalar/Vectorial ya residentes calculan el resultado), usando `mesh_.load_instr(...)`
para reprogramar Enrutamiento/Memoria y `memory_cell().dma_done()` para esperar cada
transferencia del DMA local antes de avanzar a la siguiente fase.

Puntos clave:
- `CONFIG` es el momento en que se programa la celda (no `START`) — `START` solo
  dispara ejecución/lectura sobre lo que ya está cargado. Por eso una segunda ronda
  de datos puede repetir `START` sin repetir `CONFIG` (el programa sigue residente en
  la memoria de instrucciones de la PE).
- `clk`/`rst`/`enable` del mesh son enteramente internos a `MeshWrapper` — no se
  exponen como puertos. `MeshWrapper` se resetea una única vez al arrancar la
  simulación (`reset_thread`, `SC_THREAD` sin loop, ya que no existe una transacción
  "RESET" en el mapa de registros). Cualquier transacción que llegue antes de que ese
  reset termine simplemente espera (`wait(reset_done_event_)` al tope de
  `b_transport`) — no falla, se comporta como si el bus tardara un poco más en
  drenar la primera transacción.
- `wait()` dentro de `b_transport` es legal porque `simple_target_socket` ejecuta el
  callback en el contexto del hilo llamante (`FakeCsrDma::run`, un `SC_THREAD`) — el
  mismo principio que ya usa `CSR_DMA::execute_cgra()` (ahí del lado iniciador,
  esperando 10ns entre cada poll de `DONE`).
- Los márgenes de tiempo (reset, latch de instrucción, cómputo) esperan por duración
  (`wait(N*10, SC_NS)`), no por `clk_.posedge_event()` directo: esperar el evento
  competiría, en el mismo delta cycle, con los procesos internos del mesh que también
  son sensibles a `clk.pos()` (`issue`/`pc_update`/`load_program`), con orden de
  ejecución no garantizado entre ambos. Esperar por duración es el mismo patrón ya
  validado en `mesh/CGRA_Mesh_SmokeTest__TB.cpp` (`advance_cycles()`).
- `SC_REPORT_ERROR` por defecto agrega `SC_THROW` a sus acciones en esta versión de
  SystemC (IEEE 1666-2023) — sin configuración adicional, abortaría la simulación
  completa apenas se reporta un error. `MeshWrapper_CSRDMA_Sim__TB.cpp` lo relaja
  explícitamente a `SC_DISPLAY | SC_LOG` en `sc_main` (mismo nivel que un `WARNING`,
  conservando la severidad `ERROR` en el mensaje) para poder ejercitar el caso
  "CONFIG inválido" sin abortar. Cualquier integrador que reuse `MeshWrapper` fuera de
  este testbench debe decidir su propia política de acciones para `SC_ERROR`.
- Con `ROWS=2,COLS=2` cada celda solo tiene 2 de sus 4 puertos de vecino conectados a
  bordes reales (los otros 2 son enlaces internos hacia la celda vecina) — ver el
  diagrama de adyacencia al inicio de este README y
  `mesh/CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp`. Por eso Vectorial usa S+E (sus dos
  bordes reales) para `PROGRAM_VECTOR_ADD`, y por eso `PROGRAM_FULL_PIPELINE` necesita
  releva por Enrutamiento/Memoria para hacerle llegar `b` a Escalar (que solo tiene
  W+S como bordes reales, ninguno hacia Vectorial).

## Simplificaciones conscientes

- **Asimetría de tamaño INPUT/OUTPUT**: `INPUT_DATA_BUFFER` son 32 bytes (2 operandos
  vectoriales) pero `OUTPUT_DATA_BUFFER` son solo 16 (1 resultado). Un `CSR_DMA` real
  asume el mismo `data_size` para ambos (`read_from_memory()`/`receive_output_data_from_cgra()`
  en `csr_dma.cpp` comparten el mismo campo `data_size`) — integrar `mesh_wrapper` de
  verdad detrás de `cgra_socket` requeriría resolver esa asimetría (por ejemplo,
  aceptando un tamaño de entrada mayor que el de salida, o separando ambos conceptos
  en el CSR real).
- **`wait()` dentro de `b_transport`**: desviación del estilo TLM-2.0
  "loosely-timed puro" (que preferiría modelar el tiempo vía el parámetro `delay` en
  vez de bloquear el hilo llamante) — aceptable aquí porque el testbench tiene un
  único hilo iniciador y no necesita ser reentrante.
- **Puerto NoC único en Memoria**: `PE_Memory_Mesh_Cell` solo ata su NoC al borde
  oeste (hacia Enrutamiento) — ver "Simplificaciones conscientes" en
  `../memory/PE_Memory_Mesh_Cell.h`. Por eso el operando que atraviesa Memoria en
  `PROGRAM_FULL_PIPELINE` (`b`) tiene que ser uniforme entre lanes: ni Memoria ni
  Escalar preservan 4 lanes independientes, solo lane 0.
- **Bug real corregido en `PE_Routing_Cell::bridge_instr_in`** (`../pe/routing/PE_Routing_Cell.h`):
  ese método corre con cualquier cambio de `instr_in`, incluido `mesh.clear_instr()`
  (que escribe `valid=false, addr=0` por default-construction de `InstrIn`). La
  versión original seguía `in.addr` ciegamente para decidir `ctx_sel` incluso en
  ese caso, así que **cada `clear_instr()` revertía el contexto activo a 0** —
  invisible mientras solo se usara el contexto 0, pero rompía en silencio
  cualquier relay que dependiera de un `ctx_sel` distinto de 0 (como la fase 4 de
  `PROGRAM_SUM_REDUCTION`/`PROGRAM_FULL_PIPELINE`, que activa `ctx1` y lo necesita
  vivo bastante después de que su propio `clear_instr()` ya se ejecutó). Corregido
  para que `ctx_sel` solo seposicione en una carga realmente válida.
- **`CSR_DMA::done` y kernels encadenados**: ejecutar dos kernels seguidos en la misma
  simulación (como hace `RiscvCore::run()` con `test_vector_add()` +
  `test_full_pipeline()`) expuso una condición de carrera preexistente en
  `csr_dma.cpp` — `done` se limpiaba solo dentro de `dma_controller()`, no en el
  momento en que se escribe `START`, así que un segundo kernel podía leer el `done=1`
  que dejó el kernel anterior antes de que el hilo de `dma_controller()` alcanzara a
  correr. Corregido limpiando `done` sincrónicamente en el mismo `b_transport` que
  dispara `start_dma_event` (ver el comentario en `csr_dma.cpp`, case `0x10`).
