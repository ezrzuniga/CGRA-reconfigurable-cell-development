add_executable(PE_scalar_HLS__TB scalar/PE_scalar_HLS__TB.cpp)
target_include_directories(PE_scalar_HLS__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_scalar_HLS__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_vector_HLS__TB vector/PE_vector_HLS__TB.cpp)
target_include_directories(PE_vector_HLS__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_vector_HLS__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_MAC_HLS__TB mac/PE_MAC_HLS__TB.cpp)
target_include_directories(PE_MAC_HLS__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_MAC_HLS__TB PRIVATE "${SYSTEMC_LIBRARY}")
