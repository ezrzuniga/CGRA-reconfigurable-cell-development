add_executable(MeshWrapper_CSRDMA_Sim__TB
  MeshWrapper_CSRDMA_Sim__TB.cpp
  mesh_wrapper.cpp
)
target_include_directories(MeshWrapper_CSRDMA_Sim__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/mac"
  "${CMAKE_CURRENT_SOURCE_DIR}/../mesh"
)
target_link_libraries(MeshWrapper_CSRDMA_Sim__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(MeshWrapper_SumReduction__TB
  MeshWrapper_SumReduction__TB.cpp
  mesh_wrapper.cpp
)
target_include_directories(MeshWrapper_SumReduction__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/mac"
  "${CMAKE_CURRENT_SOURCE_DIR}/../mesh"
)
target_link_libraries(MeshWrapper_SumReduction__TB PRIVATE "${SYSTEMC_LIBRARY}")
