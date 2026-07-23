
############################################################
# run_hls.tcl
# Automated Vitis HLS 2024.1 flow for sum_reduction_kernel
# Target: AMD Kria KV260 (xck26-sfvc784-2LV-c)
# Clock:  250 MHz (period = 4 ns)
#
# Usage:
#   vitis_hls -f run_hls.tcl
############################################################

set PROJECT_NAME "sum_reduction_kernel_prj"
set SOLUTION_NAME "solution1"
set TOP_FUNC      "sum_reduction_kernel"
set CLK_PERIOD_NS 4.0
;# KV260 SoM part. If Vitis HLS reports "part not found", uncomment
;# the alternate set_part line below that uses the board file instead.
set FPGA_PART     "xck26-sfvc784-2LV-c"
# set FPGA_PART   "xilinx.com:kv260:1.1"

open_project -reset $PROJECT_NAME

add_files sum_reduction_kernel.cpp
add_files -tb tb_sum_reduction.cpp -cflags "-Wno-unknown-pragmas"

set_top $TOP_FUNC

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

puts "\n===== Done. Check ${PROJECT_NAME}/${SOLUTION_NAME}/syn/report for timing/latency. ====="

exit
