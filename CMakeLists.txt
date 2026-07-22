cmake_minimum_required(VERSION 3.16)

project(CgraReconfigurableCell)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(_SYSTEMC_HINTS)
if(DEFINED ENV{SYSTEMC_HOME})
  list(APPEND _SYSTEMC_HINTS "$ENV{SYSTEMC_HOME}/include" "$ENV{SYSTEMC_HOME}/src")
endif()
list(APPEND _SYSTEMC_HINTS "/home/jeffrey/systemc-3.0.1/src" "/home/jeffrey/systemc-3.0.1/build/src/.libs")

set(_SYSTEMC_LIB_HINTS)
if(DEFINED ENV{SYSTEMC_HOME})
  list(APPEND _SYSTEMC_LIB_HINTS "$ENV{SYSTEMC_HOME}/lib" "$ENV{SYSTEMC_HOME}/build/src/.libs")
endif()
list(APPEND _SYSTEMC_LIB_HINTS "/home/jeffrey/systemc-3.0.1/build/src/.libs" "/home/jeffrey/systemc-3.0.1/build/src")

find_path(
  SYSTEMC_INCLUDE_DIR
  systemc.h
  HINTS ${_SYSTEMC_HINTS}
  PATHS /usr/include /usr/local/include
  REQUIRED
)

set(SYSTEMC_LIBRARY "/home/jeffrey/systemc-3.0.1/build/src/.libs/libsystemc-3.0.0.so")
if(NOT EXISTS "${SYSTEMC_LIBRARY}")
  find_library(
    SYSTEMC_LIBRARY
    NAMES systemc systemc-3.0.0
    HINTS ${_SYSTEMC_LIB_HINTS}
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
    REQUIRED
  )
endif()

add_subdirectory(pe)
add_subdirectory(mesh)
add_subdirectory(mesh_wrapper)
add_subdirectory(riscv_dma_main_mem_components)
