add_executable(PE_Memory_HLS_Cell__TB PE_Memory_HLS_Cell__TB.cpp)
target_include_directories(PE_Memory_HLS_Cell__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_Memory_HLS_Cell__TB PRIVATE "${SYSTEMC_LIBRARY}")
