// sum_reduction_kernel.cpp
// Synthesizable HLS re-implementation of PROGRAM_SUM_REDUCTION
// (../mesh_wrapper/mesh_wrapper.cpp, MeshWrapper::run_sum_reduction_dataflow):
// total = seed + sum(v[0..SUM_REDUCTION_VECTOR_LEN-1]).
//
// The SystemC/TLM MeshWrapper models that reduction as an instruction stream
// walking the CGRA mesh (Enrutamiento -> Memoria -> Enrutamiento -> Escalar,
// one new accumulate instruction per element); none of that -- tlm sockets,
// tlm_generic_payload, a self-instantiated sc_clock -- is synthesizable HLS
// C/C++. This kernel keeps only the arithmetic contract (same inputs, same
// total) so it can go through csim/csynth/cosim, using
// ../mesh_wrapper/MeshWrapper_SumReduction__TB.cpp's three rounds as the
// golden reference (see tb_sum_reduction.cpp).

#include "sum_reduction_kernel.h"

void sum_reduction_kernel(int32_t seed, int32_t v[SUM_REDUCTION_VECTOR_LEN], int32_t *out) {
#pragma HLS INTERFACE s_axilite port=seed   bundle=control
#pragma HLS INTERFACE s_axilite port=v      bundle=control
#pragma HLS INTERFACE s_axilite port=out    bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    int32_t total = seed;
    for (int i = 0; i < SUM_REDUCTION_VECTOR_LEN; i++) {
#pragma HLS UNROLL
        total += v[i];
    }
    *out = total;
}
