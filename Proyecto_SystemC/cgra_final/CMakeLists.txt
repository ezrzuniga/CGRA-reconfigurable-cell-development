add_executable(CGRA_Final_Mesh__TB CGRA_Final_Mesh__TB.cpp)
target_include_directories(CGRA_Final_Mesh__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(CGRA_Final_Mesh__TB PRIVATE "${SYSTEMC_LIBRARY}")
