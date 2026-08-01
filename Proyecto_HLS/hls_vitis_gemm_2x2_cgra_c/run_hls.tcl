
############################################################
# run_hls.tcl
# Automated Vitis HLS 2024.1 flow for GEMM_2x2_HLS_Top_C
# Target: AMD Kria KV260 (xck26-sfvc784-2LV-c)
# Clock:  250 MHz (period = 4 ns)
#
# Variante C/C++ pura de ../hls_vitis_gemm_2x2_cgra (que falla en
# csynth_design con "ERROR: [HLS 200-637] SystemC input is not supported":
# Vitis HLS 2024.1, flujo unificado, rechaza cualquier top escrito como
# sc_module). Sintetiza la misma CGRA real -- GEMM_2x2_HLS_Top_C instancia
# CGRA_Mesh_Static_C<2,2,32,1, 4 celdas PE_MAC> sin modificarla
# (../../Proyecto/mesh_hls_c, ../../Proyecto/pe_hls_c) -- pero sin
# sc_module/puertos/sc_signal: la FSM corre completa en una sola invocacion
# de la funcion top. Ver ../../Proyecto/gemm_hls_c/GEMM_2x2_HLS_Top_C.h para
# el diseno completo y el README de esta carpeta para la relacion con la
# variante SystemC-HLS bloqueada.
#
# Usage:
#   vitis-run --mode hls --tcl run_hls.tcl
############################################################

set PROJECT_NAME "gemm_2x2_cgra_c_prj"
set SOLUTION_NAME "solution1"
set TOP_MODULE    "GEMM_2x2_HLS_Top_C"
set CLK_PERIOD_NS 4.0
;# KV260 SoM part. Si Vitis HLS reporta "part not found", descomentar la
;# linea alternativa de mas abajo que usa el board file en su lugar.
set FPGA_PART     "xck26-sfvc784-2LV-c"
# set FPGA_PART   "xilinx.com:kv260:1.1"

open_project -reset $PROJECT_NAME

add_files gemm_2x2_hls_top_c.cpp -cflags "-std=c++17"
add_files -tb ../../Proyecto/gemm_hls_c/GEMM_2x2_HLS_Top_C__TB.cpp -cflags "-std=c++17 -Wno-unknown-pragmas"

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
