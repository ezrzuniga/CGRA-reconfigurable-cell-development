
############################################################
# run_hls.tcl
# Automated Vitis HLS 2024.1 flow for GEMM_2x2_HLS_Top
# Target: AMD Kria KV260 (xck26-sfvc784-2LV-c)
# Clock:  250 MHz (period = 4 ns)
#
# A diferencia de hls_vitis_sum_reduction (una funcion C++ plana), esto
# sintetiza la CGRA real: GEMM_2x2_HLS_Top instancia CGRA_Mesh_Static<2,2,32,1,
# PE_MAC_Cell_HLS x4> sin modificarla (../../Proyecto/mesh_hls,
# ../../Proyecto/pe_hls) detras de una FSM propia que reemplaza el
# load_instr()/advance_cycles() del testbench de simulacion por hardware real.
# Ver ../../Proyecto/gemm_hls/GEMM_2x2_HLS_Top.h para el diseno completo y el
# README de esta carpeta para riesgos conocidos del subset SystemC de HLS.
#
# Usage:
#   vitis_hls -f run_hls.tcl
############################################################

set PROJECT_NAME "gemm_2x2_cgra_prj"
set SOLUTION_NAME "solution1"
set TOP_MODULE    "GEMM_2x2_HLS_Top"
set CLK_PERIOD_NS 4.0
;# KV260 SoM part. Si Vitis HLS reporta "part not found", descomentar la
;# linea alternativa de mas abajo que usa el board file en su lugar.
set FPGA_PART     "xck26-sfvc784-2LV-c"
# set FPGA_PART   "xilinx.com:kv260:1.1"

open_project -reset $PROJECT_NAME

add_files gemm_2x2_hls_top.cpp
add_files -tb ../../Proyecto/gemm_hls/GEMM_2x2_HLS_Top__TB.cpp -cflags "-Wno-unknown-pragmas"

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
