# PE Scalar Design

Modelo en SystemC de un Processing Element (PE) escalar, pensado como IP reusable para
una CGRA. Ejecuta un ISA propio inspirado en RISC-V sobre sus 4 puertos de malla
(Norte/Sur/Este/Oeste) y un banco de registros interno.

## Requisitos
- SystemC (variable de entorno `SYSTEMC_HOME` apuntando a la instalación, si no está en
  una ruta estándar del sistema).
- CMake >= 3.16 y un compilador con soporte C++17.
- (Opcional) [GTKWave](https://gtkwave.sourceforge.net/) para ver el waveform generado.

## Compilar
```
mkdir -p build && cd build
cmake ..
make
```

## Ejecutar
```
./PE_scalar__TB
```
Corre un smoke test que programa la PE con `out_E = in_N + in_S` y valida el resultado
(incluyendo un caso de broadcast `DST_ALL`). También genera `pe_scalar_wave.vcd` con el
estado interno de la simulación, visible con:
```
gtkwave pe_scalar_wave.vcd
```

```
./ALU_scalar__TB
```
Corre un vector de prueba por cada opcode de la ALU (aritmética/lógica RV32I completa +
`MUL`), incluyendo casos límite de shifts, comparación con/sin signo, y overflow de `MUL`.

## Estructura
- `pe_isa.h` — opcodes e instrucciones de la PE.
- `ALU_scalar.h` — ALU entera combinacional.
- `PE_scalar.h` — top: memoria de instrucciones, banco de registros, control de
  ejecución y wiring hacia la ALU.
- `PE_scalar__TB.cpp` — testbench/smoke test de la PE completa.
- `ALU_scalar__TB.cpp` — testbench dedicado a la ALU.

Más detalle de arquitectura y decisiones de diseño en `CLAUDE.md`.
