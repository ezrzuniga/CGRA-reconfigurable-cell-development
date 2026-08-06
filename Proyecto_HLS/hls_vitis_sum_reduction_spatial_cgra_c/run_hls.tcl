
############################################################
# run_hls.tcl
# Automated Vitis HLS 2024.1+ flow for SumReduction_Spatial_Top_C
# Target: AMD Kria KV260 (xck26-sfvc784-2LV-c)
# Clock:  250 MHz (period = 4 ns)
#
# Mapeo ESPACIAL de la reduccion por suma: 4 celdas PE_Scalar en una malla
# 2x2 (CGRA_Mesh_Static_C<2,2,32,1, 4x PE_Scalar_State>), 1 sola fase, arbol
# de sumas de 3 niveles (profundidad log2(8)) en vez de 8 acumulaciones
# secuenciales sobre 1 celda. Contraparte de
# ../hls_vitis_sum_reduction_temporal_cgra_c -- mismos 2 casos de prueba en
# ambos testbenches para poder comparar directamente latencia/recursos de
# csynth.rpt entre los dos mapeos del MISMO problema. Ver
# ../../Proyecto_C/sum_reduction_hls_c/README.md para el diseno completo.
#
# Usage:
#   vitis-run --mode hls --tcl run_hls.tcl
############################################################

set PROJECT_NAME "sum_reduction_spatial_cgra_c_prj"
set SOLUTION_NAME "solution1"
set TOP_MODULE    "SumReduction_Spatial_Top_C"
set CLK_PERIOD_NS 4.0
;# KV260 SoM part. Si Vitis HLS reporta "part not found", descomentar la
;# linea alternativa de mas abajo que usa el board file en su lugar.
set FPGA_PART     "xck26-sfvc784-2LV-c"
# set FPGA_PART   "xilinx.com:kv260:1.1"

open_project -reset $PROJECT_NAME

add_files sum_reduction_spatial_hls_top_c.cpp -cflags "-std=c++17"
add_files -tb ../../Proyecto_C/sum_reduction_hls_c/SumReduction_Spatial_Top_C__TB.cpp -cflags "-std=c++17 -Wno-unknown-pragmas"

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
