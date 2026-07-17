# PE Vector Design

Modelo en SystemC de un Processing Element (PE) vectorial, adaptado a partir del
PE escalar `pe_scalar`. Ejecuta el mismo ISA de instrucciones sobre vectores de
lanes paralelos en lugar de datos escalares.

## Requisitos
- SystemC (variable de entorno `SYSTEMC_HOME` apuntando a la instalación, si no
  está en una ruta estándar del sistema).
- CMake >= 3.16 y un compilador con soporte C++17.
- (Opcional) [GTKWave](https://gtkwave.sourceforge.net/) para ver el waveform
  generado.

## Compilar
```
mkdir -p build && cd build
cmake ..
make
```

## Ejecutar
```
./ALU_vector__TB
```
Corre un vector de prueba por cada opcode de la ALU vectorial.

```
./PE_vector__TB
```
Corre un smoke test para la PE vectorial: suma por lanes, broadcast y reset.

## Estructura
- `pe_isa.h` — ISA vectorial y tipo de datos de vector.
- `ALU_vector.h` — ALU combinacional que opera por lanes.
- `PE_vector.h` — top vectorial con memoria de instrucciones, banco de registros
  vectoriales y control de ejecución.
- `PE_vector__TB.cpp` — testbench de la PE vectorial.
- `ALU_vector__TB.cpp` — testbench dedicado a la ALU vectorial.
