# mesh_wrapper

Puente TLM-2.0 <-> señales planas sobre una `CGRA_Mesh_Heterogeneous<1,1>` de una
sola `PE_vector`. Expone un único `tlm_utils::simple_target_socket` con el mismo mapa
de registros que `riscv_dma_main_mem_components/csr_dma.cpp` ya asume del lado
`cgra_socket`, y traduce cada transacción en `mesh.load_instr(...)`/pokes sobre los
puertos de borde del mesh real (`mesh/CGRA_Mesh_Heterogeneous.h`).

**Alcance de esta carpeta**: standalone. No se instancia `CSR_DMA`/`RiscvCore`/
`MainMemory` reales — el testbench trae su propio módulo de test (`FakeCsrDma`) que
imita el protocolo. No se modifica `riscv_dma_main_mem_components/` ni `top.cpp` en
esta etapa; ver "Simplificaciones conscientes" más abajo para lo que falta para una
integración de verdad.

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
./MeshWrapper_CSRDMA_Sim__TB
```
Genera `mesh_wrapper_csr_dma_sim_wave.vcd` en el directorio desde el que se ejecuta,
con las señales de control/borde del mesh interno más el estado de la PE
(`wrapper.trace(tf)` reenvía a `mesh_.trace(tf)`). Se puede inspeccionar con
`gtkwave mesh_wrapper_csr_dma_sim_wave.vcd`.

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
programas pre-armados, definido en `mesh_wrapper.h`:

| Valor | Nombre               | Instrucción cargada en `pe(0,0)`                        |
|-------|----------------------|----------------------------------------------------------|
| `0`   | `PROGRAM_VECTOR_ADD` | `opcode=OP_ADD, src_a=SRC_WEST, src_b=SRC_NORTH, dst=DST_EAST` |

Cualquier otro valor genera `SC_REPORT_ERROR` (la transacción sigue respondiendo
`TLM_OK_RESPONSE`, el catálogo simplemente no encontró un programa para ese índice).

**Nota**: el mesh interno se instancia con `INSTR_MEM_SIZE=1` (no el default de 16).
El PC de `PE_vector` es un contador libre que incrementa todos los ciclos habilitados
(no hay saltos/branch en el ISA), así que `pc % INSTR_MEM_SIZE` solo vale `0` de forma
permanente si `INSTR_MEM_SIZE=1` — con un valor mayor, el PC abandona la dirección
`0` (donde `load_instr` escribe) en el primer ciclo habilitado y nunca vuelve. Mismo
motivo por el que `mesh/CGRA_Mesh_SmokeTest__TB.cpp`/`ComplexTest__TB.cpp` usan
siempre `addr=0` con `INSTR_MEM_SIZE=1`.

Para agregar una entrada nueva:
1. Nuevo valor en el `enum MeshProgram` (`mesh_wrapper.h`).
2. Nueva rama en `MeshWrapper::build_instruction` (`mesh_wrapper.cpp`).
3. Actualizar esta tabla.

## Protocolo de programación y control

```
FakeCsrDma (SC_THREAD "run")                  MeshWrapper::b_transport                 CGRA_Mesh_Heterogeneous<1,1> "mesh_"
--------------------------------              ---------------------------------------  ------------------------------------
1. TLM WRITE addr=0x00 (CONFIG)      ------->  handle_config_write(PROGRAM_VECTOR_ADD)
                                                 build_instruction() -> Instr{ADD,W,N,E}
                                                 mesh_.load_instr(0,0,0,instr) --------> instr_sig[0].write(...)  (poke)
                                                 wait(clk_.posedge_event())    --------> [1 flanco clk] PE latchea instr_in
                                                 mesh_.clear_instr(0,0)        --------> instr_sig[0].write(InstrIn())
                                                 programmed_ = true
                                                 <--- TLM_OK_RESPONSE

2. TLM WRITE addr=0x10 (INPUT, 32B)   ------->  handle_input_write(data,32)
                                                 A = bytes[0:16), B = bytes[16:32)
                                                 in_W_.write(Link(A))          --------> mesh_.in_W[0] (borde real, ROWS=1)
                                                 in_N_.write(Link(B))          --------> mesh_.in_N[0] (borde real, COLS=1)
                                                 <--- TLM_OK_RESPONSE

3. TLM WRITE addr=0x04 (START=1)      ------->  handle_start_write(1)
                                                 [guard: reset_done_, programmed_]
                                                 status_=1; done_=0
                                                 wait(2 flancos clk)           --------> PE computa OP_ADD(in_W,in_N)->out_E
                                                 result_ = out_E_.read()       <-------- mesh_.out_E[0]
                                                 status_=0; done_=1
                                                 <--- TLM_OK_RESPONSE

4. TLM READ addr=0x0C (DONE), loop    ------->  *data = done_ (poll con wait(10,SC_NS)
   hasta leer 1                                 entre intentos, igual que
                                                 CSR_DMA::execute_cgra())

5. TLM READ addr=0x14 (OUTPUT, 16B)   ------->  handle_output_read(data,16)
                                                 memcpy(data, result_, 16)
                                                 <--- TLM_OK_RESPONSE
```

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
- Con `ROWS=1,COLS=1` la única celda del mesh tiene sus 4 puertos de vecino
  conectados directamente a los 4 bordes externos — no hay celdas de relevo. El
  wiring de `SRC_WEST`+`SRC_NORTH`→`DST_EAST` usa 2 de esos 4 bordes como operandos y
  el tercero como salida (mismo patrón ya usado por `PE_scalar` en
  `mesh/CGRA_Mesh_SmokeTest__TB.cpp` fila 0, aplicado aquí a la `PE_vector`).

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
- **Una sola PE, un solo programa**: el catálogo (`MeshProgram`) tiene una entrada, y
  la malla es `1x1`. Crecer a más celdas o más programas es una extensión
  intencionalmente fuera de esta etapa (ver `SESSION_CONTEXT_riscv_dma_mesh.md` en la
  raíz del repo para las preguntas abiertas de integración completa).
