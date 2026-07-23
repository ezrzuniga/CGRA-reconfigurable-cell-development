// sum_reduction_kernel.h
// SUM_REDUCTION_VECTOR_LEN mirrors ../mesh_wrapper/mesh_wrapper.h (7 elements,
// seed carried separately -- see that file's comment on INPUT_DATA_BUFFER).

#ifndef SUM_REDUCTION_KERNEL_H
#define SUM_REDUCTION_KERNEL_H

#include <cstdint>

static const int SUM_REDUCTION_VECTOR_LEN = 7;

void sum_reduction_kernel(int32_t seed, int32_t v[SUM_REDUCTION_VECTOR_LEN], int32_t *out);

#endif // SUM_REDUCTION_KERNEL_H
