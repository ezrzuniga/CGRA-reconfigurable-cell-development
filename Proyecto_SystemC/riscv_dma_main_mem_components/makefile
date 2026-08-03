#=========================================================
# Project name
#=========================================================

TARGET = cgra_platform


#=========================================================
# Compiler
#=========================================================

CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17


#=========================================================
# SystemC installation
#   Se puede sobreescribir con: make SYSTEMC_HOME=/ruta
#=========================================================

SYSTEMC_HOME ?= /usr/local/systemc

INCLUDES = -I$(SYSTEMC_HOME)/include \
           -I../mesh_wrapper \
           -I../pe/scalar \
           -I../pe/vector \
           -I../pe/mac \
           -I../mesh

LIBS = -L$(SYSTEMC_HOME)/lib-linux64 \
       -lsystemc \
       -lm


#=========================================================
# Source files
#
# Entry point: RiscvDmaSystem__TB.cpp (sc_main real del sistema completo
# RiscvCore -> CSR_DMA -> MainMemory -> MeshWrapper, con el MemoryRouter fan-in
# que MainMemory necesita para aceptar CPU y DMA a la vez). Corre la suite del
# RISC-V: vector-add, full-pipeline y matmul 2x2.
#
# Nota: top.cpp / riscv_dma_main_mem_components_tb.cpp (el entry basado en `Top`)
# hoy bindea MainMemory::socket dos veces sobre un simple_target_socket punto a
# punto y no elabora; por eso el build apunta al TB que si funciona.
#=========================================================

SOURCES = \
	RiscvDmaSystem__TB.cpp \
	riscv_core.cpp \
	csr_dma.cpp \
	main_memory.cpp \
	../mesh_wrapper/mesh_wrapper.cpp


#=========================================================
# Build target (compilacion y link en un solo paso: hay fuentes en otro
# directorio, evitar objetos sueltos cruzados)
#=========================================================

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $(TARGET) $(LIBS)


#=========================================================
# Run the simulation
#=========================================================

run: $(TARGET)
	LD_LIBRARY_PATH=$(SYSTEMC_HOME)/lib-linux64 ./$(TARGET)


#=========================================================
# Clean generated files
#=========================================================

clean:
	rm -f *.o $(TARGET)


#=========================================================
# Rebuild everything
#=========================================================

rebuild: clean all
