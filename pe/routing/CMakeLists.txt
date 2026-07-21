add_executable(Routing_Cell__TB Routing_Cell__TB.cpp)
target_include_directories(Routing_Cell__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
)
target_link_libraries(Routing_Cell__TB PRIVATE "${SYSTEMC_LIBRARY}")

add_executable(Routing_Cell_PE_Integration__TB Routing_Cell_PE_Integration__TB.cpp)
target_include_directories(Routing_Cell_PE_Integration__TB PRIVATE
  "${SYSTEMC_INCLUDE_DIR}"
)
target_link_libraries(Routing_Cell_PE_Integration__TB PRIVATE "${SYSTEMC_LIBRARY}")
