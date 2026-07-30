add_executable(GEMM_2x2_MAC__TB GEMM_2x2_MAC__TB.cpp)
target_include_directories(GEMM_2x2_MAC__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(GEMM_2x2_MAC__TB PRIVATE "${SYSTEMC_LIBRARY}")
