# PE Design

Modelos en SystemC de los Processing Elements (PE) de la CGRA, pensados como IP
reusable: 3 variantes (`scalar/`, `vector/`, `mac/`) que comparten el mismo ISA propio
inspirado en RISC-V (mismos mnemonics ADD/SUB/AND/OR/XOR) sobre 4 puertos de malla
(Norte/Sur/Este/Oeste), más el contrato de puertos (`PE_Base.h`) que las adapta a la
malla heterogénea de `../mesh/`.

## Requisitos
- SystemC (variable de entorno `SYSTEMC_HOME` apuntando a la instalación, si no está en
  una ruta estándar del sistema).
- CMake >= 3.16 y un compilador con soporte C++17.
- (Opcional) [GTKWave](https://gtkwave.sourceforge.net/) para ver el waveform generado.

## Compilar
Desde la raíz del repo (el build está unificado, ver `CMakeLists.txt` raíz):
```
mkdir -p build && cd build
cmake ..
make
```
Los binarios de esta carpeta quedan en `build/pe/`.

## Ejecutar
```
./ALU_scalar__TB   # vectores de prueba de la ALU escalar
./PE_scalar__TB    # smoke test de la PE escalar
./ALU_vector__TB   # vectores de prueba de la ALU vectorial (SIMD, VLEN lanes)
./PE_vector__TB    # smoke test de la PE vectorial
./ALU_MAC__TB      # vectores de prueba de la ALU MAC
./PE_MAC__TB       # smoke test de la PE MAC (multiply-accumulate de 1 ciclo)
```
Los smoke tests de PE generan además `pe_scalar_wave.vcd`/`pe_vector_wave.vcd`/
`pe_mac_wave.vcd` con el estado interno de la simulación, visibles con:
```
gtkwave pe_scalar_wave.vcd
```

## Estructura
- `pe_isa.h` — ISA compartido por las 3 variantes: opcodes, formato de instrucción y el
  tipo de dato vectorial.
- `PE_Base.h` — contrato de puertos uniforme para que la malla heterogénea (`../mesh/`)
  pueda tratar cualquier variante de PE de forma intercambiable.
- `scalar/` — PE escalar: `ALU_scalar.h`, `PE_scalar.h`, `PE_Scalar_Cell.h` (adaptador
  de malla) y sus testbenches.
- `vector/` — PE vectorial (SIMD de ancho fijo): `ALU_vector.h`, `PE_vector.h`,
  `PE_Vector_Cell.h` y sus testbenches.
- `mac/` — PE de multiply-accumulate vectorial de 1 ciclo: `ALU_MAC.h`, `PE_MAC.h`,
  `PE_MAC_Cell.h` y sus testbenches.

Más detalle de arquitectura y decisiones de diseño en `CLAUDE.md`.
