# cgra_final_noc/gem5/ — los 4 kernels como workload de CPU host (gem5 SE)

Cuatro binarios standalone (`gemm_workload.c`, `sum_reduce8_workload.c`,
`fft4_workload.c`, `softmax4_workload.c`) que reproducen el **contrato
aritmético** de cada kernel (mismas entradas, misma salida, mismos 3 casos y
semilla `20260810` que los testbenches de `../` y `../vitis/`), pensados
para correr bajo gem5 en modo SE (syscall emulation) sobre distintos modelos
de CPU.

## Por qué esto NO es "la CGRA corriendo en gem5"

gem5 modela una **CPU de propósito general** ejecutando un binario — no
puede simular la malla NoC 3×3 heterogénea real (eso es lo que hace
`../vitis/` vía Vitis HLS: sintetiza esa malla de verdad, con su propio
reloj/área/latencia). Este binario reproduce solo el contrato aritmético
para poder comparar "la misma tarea corrida en un CPU host" vs. "la misma
tarea corrida en el acelerador CGRA" — mismo precedente que
`../../../Proyecto_GEM5/sum_reduction_workload.c` ya estableció (para un
diseño distinto, la reducción de 7 elementos de `mesh_wrapper`).

## 1) Requirements

- gem5 compilado con el target `ALL` (o al menos `X86`) —
  `build/ALL/gem5.opt` o `build/X86/gem5.opt`.
- `gcc` para compilar los workloads.
- **No hay cross-compilador RISC-V instalado en esta máquina**
  (`riscv64-*-gcc` no encontrado), así que los binarios se compilan nativos
  x86_64 (`--isa x86` en `gem5_se_config.py`, default). Si en algún momento
  se instala uno, `--isa riscv` sigue funcionando sin tocar el script (para
  que coincida con el core RISC-V real del proyecto CGRA, ver
  `../../../Proyecto_GEM5/README.md` si existe uno análogo).
- `glibc-static` instalado (`sudo dnf install glibc-static`) — los binarios
  se compilan **estáticos** (`gcc -O0 -static`), igual que recomienda
  `../../../Proyecto_GEM5/sum_reduction_workload.c`. Ver la nota de
  resultados más abajo: el link estático NO redujo el conteo de
  instrucciones/ciclos frente al dinámico (probado explícitamente, ambos
  corridos) — el costo dominante es el arranque de glibc en sí (locale,
  stdio, resolución de IFUNCs), presente en ambos modos de link por igual.

## 2) Cómo compilar y correr

```bash
gcc -O0 -static -o bin/gemm_workload gemm_workload.c
# (o sum_reduce8_workload.c / fft4_workload.c / softmax4_workload.c)

source /ruta/a/gem5/build/ALL/gem5.opt  # no hace falta "source": es el binario
/ruta/a/gem5/build/ALL/gem5.opt -d m5out/gemm_atomic gem5_se_config.py \
    --binary bin/gemm_workload --isa x86 --cpu atomic
```

`-O0` a propósito (ver comentario de cabecera de cada `.c`): a plena
optimización el compilador colapsa estos kernels (aritmética sobre
constantes conocidas en tiempo de compilación) a un resultado precomputado,
y el conteo de ciclos que gem5 mide deja de reflejar "ejecutar el kernel
real".

Modelos de CPU (`--cpu`): `atomic`, `timing`, `minor`, `o3` — ver el
comentario de cabecera de `gem5_se_config.py` para qué modela cada uno.

## 3) Resultados (ya corridos, x86, `bin/*_workload`, estáticos)

`board.processor.cores.core.numCycles` / `...numInstsNotNOP` de cada
`m5out/<kernel>_<cpu>/stats.txt`:

| Kernel      | atomic (ciclos / instr.) | timing (ciclos) | minor (ciclos) | o3 |
|-------------|---------------------------|------------------|------------------|----|
| GEMM 2×2    | 323266 / 154311           | 11846328         | 3144872          | **crashea** |
| SumReduce8  | 307517 / 146966           | 11288107         | 2985020          | **crashea** |
| FFT4        | 320513 / 153039           | 11732930         | 3281532          | **crashea** |
| Softmax4    | 402392 / 190243           | 14881608         | 4269837          | **crashea** |

**`--cpu o3` (DerivO3CPU) crashea con segfault en los 4 kernels** — el
backtrace apunta a `gem5::o3::Decode::sortInsts()`, dentro del propio modelo
de CPU de gem5 (build `25.1.0.1`), no a nada de estos binarios ni de este
proyecto. No se investigó más a fondo (bug de esta build de gem5, fuera del
alcance de este cambio) — `atomic`, `timing` y `minor` sí sirven para
comparar.

**Sorpresa real (medida, no supuesta): el link estático dio MÁS
instrucciones/ciclos que el dinámico**, no menos (p.ej. GEMM: 109939→154311
instrucciones). Para aislar la causa se corrió un binario trivial
(`int main(void){return 0;}`, mismo `gcc -O0 -static`): **137565
instrucciones / 286955 ciclos (atomic) solo para arrancar y salir**. Eso
confirma que el costo dominante en los 4 kernels es el arranque de glibc en
sí (inicialización de locale/stdio, resolución de IFUNCs para
memcpy/strlen, etc. — todo esto corre igual, o incluso con más código, en
un binario estático a `-O0` que en uno dinámico), no el modo de link.
Restando esa base de 137565 instrucciones para aislar el costo marginal de
cada kernel (aritmética real + sus `printf` de reporte):

| Kernel      | Instrucciones marginales | Ciclos marginales (atomic) |
|-------------|---------------------------|------------------------------|
| GEMM 2×2    | 16746                     | 36311                        |
| SumReduce8  | 9401                      | 20562                        |
| FFT4        | 15474                     | 33558                        |
| Softmax4    | 52678                     | 115437                       |

Softmax4 destaca porque su `printf` de reporte imprime 4 razones softmax
como `double` (`%.6f`) por ronda × 3 rondas = 12 llamadas de formateo de
punto flotante — eso es la mayor parte de sus 52678 instrucciones
marginales, no la aritmética `EXP2`/suma en sí (unas pocas decenas de
instrucciones reales). Ni siquiera esta cifra "marginal" es directamente
"el kernel puro": para aislar eso de verdad haría falta un binario sin
libc (`-nostdlib`, syscalls crudas para imprimir), fuera del alcance de
este cambio.

**Los conteos de arriba NO son comparables directamente con los ciclos de
malla de `../vitis/`** (58–236 ciclos según el kernel): unos miden un CPU
de propósito general con su stack de arranque completo, el otro mide un
acelerador espacial dedicado sin sistema operativo ni libc de por medio.
Sirven para comparar **entre modelos de CPU** sobre el mismo binario
(p.ej. `timing` vs `minor`: ~3.5× más ciclos en `timing` para el mismo
trabajo, por modelar latencia de memoria en detalle) y, con la resta de
línea base de arriba, para una comparación aproximada del costo marginal
entre kernels.

## 4) Files

- `gemm_workload.c` / `sum_reduce8_workload.c` / `fft4_workload.c` /
  `softmax4_workload.c` — un binario standalone por kernel, contrato
  aritmético + 3 rondas doradas (semilla `20260810`, mismos valores que
  `../` y `../vitis/`).
- `gem5_se_config.py` — config de gem5 SE mode (copia adaptada de
  `../../../Proyecto_GEM5/gem5_se_config.py`, default `--isa x86`).
- `bin/` — binarios ya compilados (x86_64, estáticos).
- `m5out/<kernel>_<cpu>/` — salida de cada corrida ya hecha (`stats.txt`,
  `run.log`).
