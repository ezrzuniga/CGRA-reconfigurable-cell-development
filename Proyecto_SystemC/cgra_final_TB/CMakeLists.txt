add_executable(CGRA_Final_GEMM__TB CGRA_Final_GEMM__TB.cpp)
target_include_directories(CGRA_Final_GEMM__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(CGRA_Final_GEMM__TB PRIVATE "${SYSTEMC_LIBRARY}")
