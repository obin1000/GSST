// =============================================================================
// GSST — GPU Encoder
// =============================================================================
// GPU-side data sampling kernel.
// The full GPU encoding kernel (CompactionV5T-style) will be added in a
// future iteration. The CPU fast encoding path in gsst.cu handles compression
// with O(1) hash-based symbol lookups.
// =============================================================================

#include <cuda_runtime.h>

#include <gsst/detail/common.hpp>
#include <gsst/detail/encoder.cuh>


namespace gsst::detail {

// =============================================================================
// GPU Sampling kernel
// =============================================================================
__global__ void sampling_kernel(const uint8_t* src, uint8_t* sample_buf, size_t block_size, size_t total_size) {
    const size_t block_start = static_cast<size_t>(blockIdx.x) * block_size;
    const size_t block_len = min(block_size, total_size - block_start);
    constexpr size_t sample_target = 16384;

    for (size_t i = threadIdx.x; i < min(block_len, sample_target); i += blockDim.x) {
        sample_buf[blockIdx.x * sample_target + i] = src[block_start + i];
    }
}

Status sample_blocks_gpu(const uint8_t* d_src, size_t src_size, size_t block_size, uint8_t* d_samples,
                         uint32_t num_blocks, cudaStream_t stream) {
    constexpr int threads = ENC_THREAD_COUNT;
    sampling_kernel<<<num_blocks, threads, 0, stream>>>(d_src, d_samples, block_size, src_size);

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess)
        return Status::ErrorCudaError;

    return Status::Success;
}

}  // namespace gsst::detail
