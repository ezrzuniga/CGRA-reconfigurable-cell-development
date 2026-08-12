// CGRA_Final_NoC_Mesh.h
// Version Network-on-Chip de cgra_final/CGRA_Final_Mesh.h: EXACTAMENTE el
// mismo layout 3x3, las mismas 5 variantes de celda sintetizables en las
// mismas 9 posiciones (nada en pe_hls/memory_hls/mesh_hls se toca ni se
// modifica), pero instanciadas sobre NoC_Mesh_Static (cgra_final_noc/NoC_Mesh_Static.h)
// en vez de sobre CGRA_Mesh_Static (mesh_hls/CGRA_Mesh_Static.h): la
// interconexion entre celdas pasa de wires punto a punto a una malla de
// routers que reenvian paquetes con header (dest_row,dest_col) -- ver
// cgra_final_noc/NoC_Packet.h para que es un NoC y en que se diferencia de la
// malla directa.
//
//   (0,0) Memoria     (0,1) Vectorial   (0,2) Escalar
//   (1,0) Routing     (1,1) MAC         (1,2) MAC
//   (2,0) Routing     (2,1) MAC         (2,2) MAC
//
// Mismos bordes reales, mismas 5 celdas con el mismo comportamiento interno
// -- ver cgra_final/CGRA_Final_Mesh.h para el mapa completo de bordes reales
// por celda, que no cambia aca. La unica diferencia observable desde afuera
// de esta malla es estructural (paquetes en vez de wires fijos, ver
// NoC_Router.h); el comportamiento ciclo a ciclo es identico -- confirmado en
// CGRA_Final_NoC_Mesh__TB.cpp reusando exactamente los mismos vectores de
// prueba que cgra_final/CGRA_Final_Mesh__TB.cpp.
//
// Igual que CGRA_Final_Mesh, esta malla NO trae un programa espacial
// pre-cargado: se programa despues via mesh.load_instr()/mesh.clear_instr(),
// como cualquier NoC_Mesh_Static.

#ifndef CGRA_FINAL_NOC_MESH_H
#define CGRA_FINAL_NOC_MESH_H

#include "NoC_Mesh_Static.h"
#include "../memory_hls/PE_Memory_HLS_Cell.h"
#include "../pe_hls/vector/PE_Vector_Cell_HLS.h"
#include "../pe_hls/scalar/PE_Scalar_Cell_HLS.h"
#include "../pe_hls/routing/PE_Routing_Cell_HLS.h"
#include "../pe_hls/mac/PE_MAC_Cell_HLS.h"

static const int CGRA_FINAL_NOC_ROWS = 3;
static const int CGRA_FINAL_NOC_COLS = 3;
static const int CGRA_FINAL_NOC_DATA_W = 32;
static const int CGRA_FINAL_NOC_VLEN = 4;
static const int CGRA_FINAL_NOC_NUM_REGS = 8;
static const int CGRA_FINAL_NOC_INSTR_MEM_SIZE = 16;

typedef NoC_Mesh_Static<CGRA_FINAL_NOC_ROWS, CGRA_FINAL_NOC_COLS, CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN,
    PE_Memory_HLS_Cell<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN>,                                                        // (0,0)
    PE_Vector_Cell_HLS<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN, CGRA_FINAL_NOC_NUM_REGS, CGRA_FINAL_NOC_INSTR_MEM_SIZE>, // (0,1)
    PE_Scalar_Cell_HLS<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN, CGRA_FINAL_NOC_NUM_REGS, CGRA_FINAL_NOC_INSTR_MEM_SIZE>, // (0,2)
    PE_Routing_Cell_HLS<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN>,                                                       // (1,0)
    PE_MAC_Cell_HLS<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN, CGRA_FINAL_NOC_NUM_REGS, CGRA_FINAL_NOC_INSTR_MEM_SIZE>,   // (1,1)
    PE_MAC_Cell_HLS<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN, CGRA_FINAL_NOC_NUM_REGS, CGRA_FINAL_NOC_INSTR_MEM_SIZE>,   // (1,2)
    PE_Routing_Cell_HLS<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN>,                                                       // (2,0)
    PE_MAC_Cell_HLS<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN, CGRA_FINAL_NOC_NUM_REGS, CGRA_FINAL_NOC_INSTR_MEM_SIZE>,   // (2,1)
    PE_MAC_Cell_HLS<CGRA_FINAL_NOC_DATA_W, CGRA_FINAL_NOC_VLEN, CGRA_FINAL_NOC_NUM_REGS, CGRA_FINAL_NOC_INSTR_MEM_SIZE>>   // (2,2)
    CGRA_Final_NoC_Mesh;

typedef CGRA_Final_NoC_Mesh::Link    CGRA_Final_NoC_Link;
typedef CGRA_Final_NoC_Mesh::Instr   CGRA_Final_NoC_Instr;
typedef CGRA_Final_NoC_Mesh::InstrIn CGRA_Final_NoC_InstrIn;
typedef CGRA_Final_NoC_Mesh::Packet  CGRA_Final_NoC_Packet;

#endif // CGRA_FINAL_NOC_MESH_H
