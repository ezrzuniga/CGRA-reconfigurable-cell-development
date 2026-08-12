add_executable(CGRA_Final_NoC_Mesh__TB CGRA_Final_NoC_Mesh__TB.cpp)
target_include_directories(CGRA_Final_NoC_Mesh__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(CGRA_Final_NoC_Mesh__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(CGRA_Final_NoC_GEMM__TB CGRA_Final_NoC_GEMM__TB.cpp)
target_include_directories(CGRA_Final_NoC_GEMM__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(CGRA_Final_NoC_GEMM__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(NoC_Router__TB NoC_Router__TB.cpp)
target_include_directories(NoC_Router__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(NoC_Router__TB PRIVATE "${SYSTEMC_LIBRARY}")
