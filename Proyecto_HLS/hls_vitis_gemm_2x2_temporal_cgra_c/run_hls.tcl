
############################################################
# run_hls.tcl
# Automated Vitis HLS 2024.2 flow for GEMM_2x2_Temporal_Top_C
# Target: AMD Kria KV260 (xck26-sfvc784-2LV-c)
# Clock:  250 MHz (period = 4 ns)
#
# Mapeo TEMPORAL de GEMM 2x2: 1 sola celda PE_MAC
# (CGRA_Mesh_Static_C<1,1,32,1, PE_MAC_State>), reutilizada 4 veces (una
# invocacion start=true por elemento de salida C[i][j], 2 fases k=0,1 cada
# una). Contraparte de ../hls_vitis_gemm_2x2_cgra_c (4 celdas PE_MAC en 2x2,
# los 4 C[i][j] se calculan en paralelo en una sola invocacion) -- mismos 2
# casos de prueba en ambos testbenches para poder comparar directamente
# latencia/recursos de csynth.rpt entre los dos mapeos del MISMO problema.
# Ver ../../Proyecto_C/gemm_temporal_hls_c/README.md para el diseno
# completo.
#
# Usage:
#   vitis-run --mode hls --tcl run_hls.tcl
############################################################

set PROJECT_NAME "gemm_2x2_temporal_cgra_c_prj"
set SOLUTION_NAME "solution1"
set TOP_MODULE    "GEMM_2x2_Temporal_Top_C"
set CLK_PERIOD_NS 4.0
;# KV260 SoM part. Si Vitis HLS reporta "part not found", descomentar la
;# linea alternativa de mas abajo que usa el board file en su lugar.
set FPGA_PART     "xck26-sfvc784-2LV-c"
# set FPGA_PART   "xilinx.com:kv260:1.1"

open_project -reset $PROJECT_NAME

add_files gemm_2x2_temporal_hls_top_c.cpp -cflags "-std=c++17"
add_files -tb ../../Proyecto_C/gemm_temporal_hls_c/GEMM_2x2_Temporal_Top_C__TB.cpp -cflags "-std=c++17 -Wno-unknown-pragmas"

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
