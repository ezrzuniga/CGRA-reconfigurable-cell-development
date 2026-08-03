add_executable(CGRA_Mesh_SmokeTest__TB CGRA_Mesh_SmokeTest__TB.cpp)
target_include_directories(CGRA_Mesh_SmokeTest__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/mac"
)
target_link_libraries(CGRA_Mesh_SmokeTest__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Mesh_ComplexTest__TB CGRA_Mesh_ComplexTest__TB.cpp)
target_include_directories(CGRA_Mesh_ComplexTest__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/mac"
)
target_link_libraries(CGRA_Mesh_ComplexTest__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Mesh_1x1_Test__TB CGRA_Mesh_1x1_Test__TB.cpp)
target_include_directories(CGRA_Mesh_1x1_Test__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/mac"
)
target_link_libraries(CGRA_Mesh_1x1_Test__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Mesh_2x2_Heterogeneous_Test__TB CGRA_Mesh_2x2_Heterogeneous_Test__TB.cpp)
target_include_directories(CGRA_Mesh_2x2_Heterogeneous_Test__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/mac"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/routing"
  "${CMAKE_CURRENT_SOURCE_DIR}/../memory"
)
target_link_libraries(CGRA_Mesh_2x2_Heterogeneous_Test__TB PRIVATE "${SYSTEMC_LIBRARY}")
