cmake_minimum_required(VERSION 3.16)

project(CgraMesh)

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

add_executable(CGRA_Mesh__TB CGRA_Mesh__TB.cpp)
target_include_directories(CGRA_Mesh__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe_scalar"
)
target_link_libraries(CGRA_Mesh__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Mesh_Pipeline__TB CGRA_Mesh_Pipeline__TB.cpp)
target_include_directories(CGRA_Mesh_Pipeline__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe_scalar"
)
target_link_libraries(CGRA_Mesh_Pipeline__TB PRIVATE "${SYSTEMC_LIBRARY}")
