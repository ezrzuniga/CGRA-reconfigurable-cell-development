add_executable(CGRA_Mesh__TB CGRA_Mesh__TB.cpp)
target_include_directories(CGRA_Mesh__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
)
target_link_libraries(CGRA_Mesh__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Mesh_Pipeline__TB CGRA_Mesh_Pipeline__TB.cpp)
target_include_directories(CGRA_Mesh_Pipeline__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
)
target_link_libraries(CGRA_Mesh_Pipeline__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Mesh_Heterogeneous__TB CGRA_Mesh_Heterogeneous__TB.cpp)
target_include_directories(CGRA_Mesh_Heterogeneous__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
)
target_link_libraries(CGRA_Mesh_Heterogeneous__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Mesh_Pipeline_Heterogeneous__TB CGRA_Mesh_Pipeline_Heterogeneous__TB.cpp)
target_include_directories(CGRA_Mesh_Pipeline_Heterogeneous__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
)
target_link_libraries(CGRA_Mesh_Pipeline_Heterogeneous__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Mesh_MAC_Heterogeneous__TB CGRA_Mesh_MAC_Heterogeneous__TB.cpp)
target_include_directories(CGRA_Mesh_MAC_Heterogeneous__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/scalar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/vector"
  "${CMAKE_CURRENT_SOURCE_DIR}/../pe/mac"
)
target_link_libraries(CGRA_Mesh_MAC_Heterogeneous__TB PRIVATE "${SYSTEMC_LIBRARY}")

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
