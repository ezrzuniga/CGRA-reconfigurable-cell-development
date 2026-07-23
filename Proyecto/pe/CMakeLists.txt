add_executable(ALU_scalar__TB scalar/ALU_scalar__TB.cpp)
target_include_directories(ALU_scalar__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(ALU_scalar__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_scalar__TB scalar/PE_scalar__TB.cpp)
target_include_directories(PE_scalar__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_scalar__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(ALU_vector__TB vector/ALU_vector__TB.cpp)
target_include_directories(ALU_vector__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(ALU_vector__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_vector__TB vector/PE_vector__TB.cpp)
target_include_directories(PE_vector__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_vector__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(ALU_MAC__TB mac/ALU_MAC__TB.cpp)
target_include_directories(ALU_MAC__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(ALU_MAC__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_MAC__TB mac/PE_MAC__TB.cpp)
target_include_directories(PE_MAC__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_MAC__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(PE_MAC_SumReduction__TB mac/PE_MAC_SumReduction__TB.cpp)
target_include_directories(PE_MAC_SumReduction__TB PRIVATE "${SYSTEMC_INCLUDE_DIR}")
target_link_libraries(PE_MAC_SumReduction__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_subdirectory(routing)
