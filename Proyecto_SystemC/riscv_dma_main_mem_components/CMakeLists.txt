cmake_minimum_required(VERSION 3.16)

project(RiscvDmaSystem)

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

if(DEFINED ENV{SYSTEMC_HOME})
  set(_SYSTEMC_PREFIX "$ENV{SYSTEMC_HOME}")
elseif(EXISTS "/usr/local/systemc")
  set(_SYSTEMC_PREFIX "/usr/local/systemc")
elseif(EXISTS "/usr/include/systemc")
  set(_SYSTEMC_PREFIX "/usr")
else()
  set(_SYSTEMC_PREFIX "")
endif()

if(_SYSTEMC_PREFIX)
  list(APPEND _SYSTEMC_HINTS "${_SYSTEMC_PREFIX}/include" "${_SYSTEMC_PREFIX}/src")
  list(APPEND _SYSTEMC_LIB_HINTS "${_SYSTEMC_PREFIX}/lib" "${_SYSTEMC_PREFIX}/lib-linux64" "${_SYSTEMC_PREFIX}/build/src/.libs")
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
  PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/local/systemc/lib-linux64
  REQUIRED
)

add_executable(RiscvDmaSystem__TB
  RiscvDmaSystem__TB.cpp
  riscv_core.cpp
  csr_dma.cpp
  main_memory.cpp
  ../mesh_wrapper/mesh_wrapper.cpp
)
target_include_directories(RiscvDmaSystem__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../mesh_wrapper"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/mac"
  "${CMAKE_CURRENT_SOURCE_DIR}/../mesh"
)
target_link_libraries(RiscvDmaSystem__TB PRIVATE "${SYSTEMC_LIBRARY}")
