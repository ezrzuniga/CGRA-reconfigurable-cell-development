
############################################################
# run_hls.tcl
# Automated Vitis HLS 2024.1 flow for CGRA_Hetero_2x2_Demo_Top_C
# Target: AMD Kria KV260 (xck26-sfvc784-2LV-c)
# Clock:  250 MHz (period = 4 ns)
#
# Prueba que la malla heterogenea 2x2 (Routing_Cell + PE_Memory + PE_Scalar +
# PE_Vector, ver ../../Proyecto_C/cgra_hetero_2x2_demo_c/) sintetiza de verdad
# -- no es una aplicacion especifica con FSM de fases como GEMM (ese sigue
# siendo cgra_run<...>/CGRA_Top_C.h): es la malla misma expuesta con un top
# minimo (programar una celda, o correr un ciclo), para que un host orqueste
# la secuencia que su aplicacion necesite.
#
# Usage:
#   vitis-run --mode hls --tcl run_hls.tcl
############################################################

set PROJECT_NAME "cgra_hetero_2x2_prj"
set SOLUTION_NAME "solution1"
set TOP_MODULE    "CGRA_Hetero_2x2_Demo_Top_C"
set CLK_PERIOD_NS 4.0
;# KV260 SoM part. Si Vitis HLS reporta "part not found", descomentar la
;# linea alternativa de mas abajo que usa el board file en su lugar.
set FPGA_PART     "xck26-sfvc784-2LV-c"
# set FPGA_PART   "xilinx.com:kv260:1.1"

open_project -reset $PROJECT_NAME

add_files cgra_hetero_2x2_top_c.cpp -cflags "-std=c++17"
add_files -tb ../../Proyecto_C/cgra_hetero_2x2_demo_c/CGRA_Hetero_2x2_Demo_Top_C__TB.cpp -cflags "-std=c++17 -Wno-unknown-pragmas"

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

puts "\n===== Done. Check ${PROJECT_NAME}/${SOLUTION_NAME}/syn/report for timing/latency/area. ====="

exit
