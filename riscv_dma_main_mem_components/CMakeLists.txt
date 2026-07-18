cmake_minimum_required(VERSION 3.16)

project(RiscvDmaSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(_SYSTEMC_HINTS)
if(DEFINED ENV{SYSTEMC_HOME})
  list(APPEND _SYSTEMC_HINTS "$ENV{SYSTEMC_HOME}/include")
endif()

set(_SYSTEMC_LIB_HINTS)
if(DEFINED ENV{SYSTEMC_HOME})
  list(APPEND _SYSTEMC_LIB_HINTS "$ENV{SYSTEMC_HOME}/lib")
endif()

find_path(
  SYSTEMC_INCLUDE_DIR
  systemc.h
  HINTS ${_SYSTEMC_HINTS}
  PATHS /usr/include /usr/local/include
  REQUIRED
)

find_library(
  SYSTEMC_LIBRARY
  systemc
  HINTS ${_SYSTEMC_LIB_HINTS}
  PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
  REQUIRED
)

add_executable(RiscvDmaSystem__TB
  RiscvDmaSystem__TB.cpp
  riscv_core.cpp
  csr_dma.cpp
  main_memory.cpp
)
target_include_directories(RiscvDmaSystem__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(RiscvDmaSystem__TB PRIVATE "${SYSTEMC_LIBRARY}")
