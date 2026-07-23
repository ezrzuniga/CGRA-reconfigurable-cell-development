cmake_minimum_required(VERSION 3.16)

project(CgraReconfigurableCell)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(_SYSTEMC_HINTS)
if(DEFINED ENV{SYSTEMC_HOME})
  list(APPEND _SYSTEMC_HINTS "$ENV{SYSTEMC_HOME}/include" "$ENV{SYSTEMC_HOME}/src")
endif()

set(_SYSTEMC_LIB_HINTS)
if(DEFINED ENV{SYSTEMC_HOME})
  list(APPEND _SYSTEMC_LIB_HINTS "$ENV{SYSTEMC_HOME}/lib" "$ENV{SYSTEMC_HOME}/build/src/.libs")
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
  NAMES systemc systemc-3.0.0
  HINTS ${_SYSTEMC_LIB_HINTS}
  PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
  REQUIRED
)

enable_testing()

add_subdirectory(pe)
add_subdirectory(mesh)
add_subdirectory(mesh_wrapper)
add_subdirectory(riscv_dma_main_mem_components)
add_subdirectory(memory)
