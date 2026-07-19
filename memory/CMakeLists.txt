cmake_minimum_required(VERSION 3.16)

project(PE_memory)

enable_testing()

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

add_executable(SRAM_dual_port__TB SRAM_dual_port__TB.cpp)
target_include_directories(SRAM_dual_port__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(SRAM_dual_port__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_Memory_Cell__TB PE_Memory_Cell__TB.cpp)
target_include_directories(PE_Memory_Cell__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_Memory_Cell__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_Memory_Cell_unit_test PE_Memory_Cell_unit_test.cpp)
target_include_directories(PE_Memory_Cell_unit_test PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_Memory_Cell_unit_test PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_Memory_Cell_interconnect_test PE_Memory_Cell_interconnect_test.cpp)
target_include_directories(PE_Memory_Cell_interconnect_test PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_Memory_Cell_interconnect_test PRIVATE "${SYSTEMC_LIBRARY}")

add_test(NAME PE_Memory_Cell_unit_test COMMAND PE_Memory_Cell_unit_test)
add_test(NAME PE_Memory_Cell_interconnect_test COMMAND PE_Memory_Cell_interconnect_test)
