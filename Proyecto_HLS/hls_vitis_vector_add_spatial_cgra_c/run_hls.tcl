
############################################################
# run_hls.tcl -- VectorAdd_Spatial_Top_C (1 PE_Vector cell, VLEN=4,
# 1 invocation, all 4 sums computed in parallel via SIMD lanes).
# Target: xck26-sfvc784-2LV-c, 4 ns / 250 MHz
# Usage: vitis-run --mode hls --tcl run_hls.tcl
############################################################
set PROJECT_NAME "vector_add_spatial_cgra_c_prj"
set SOLUTION_NAME "solution1"
set TOP_MODULE    "VectorAdd_Spatial_Top_C"
set CLK_PERIOD_NS 4.0
set FPGA_PART     "xck26-sfvc784-2LV-c"

open_project -reset $PROJECT_NAME
add_files vector_add_spatial_hls_top_c.cpp -cflags "-std=c++17"
add_files -tb ../../Proyecto_C/vector_add_hls_c/VectorAdd_Spatial_Top_C__TB.cpp -cflags "-std=c++17 -Wno-unknown-pragmas"
set_top $TOP_MODULE

open_solution -reset $SOLUTION_NAME -flow_target vivado
set_part $FPGA_PART
create_clock -period $CLK_PERIOD_NS -name default

puts "\n===== Running C Simulation ====="
csim_design
puts "\n===== Running C Synthesis ====="
csynth_design
puts "\n===== Running C/RTL Co-simulation ====="
cosim_design -rtl verilog
puts "\n===== Exporting RTL as Vivado IP ====="
export_design -flow syn -rtl verilog -format ip_catalog
puts "\n===== Done. ====="
exit
