// =============================================================================
// GSST — GPU Encoder (compression kernels)
// =============================================================================
// This file contains the GPU compression kernel stubs.
//
// The full implementation will port the high-throughput encoding pipeline from
// fsst-gpu (CompactionV5TCompressor):
//   - GPU-side data sampling (gpu_sampling kernel)
//   - Shared-memory symbol table lookups (ELL / MatchTable)
//   - Warp-level encoding with stream compaction
//   - Block-level output gathering with transpose
//
// For now, the CPU fallback in gsst.cu handles compression.
// =============================================================================

#include <gsst/detail/common.hpp>
#include <gsst/detail/encoder.cuh>


#include <cuda_runtime.h>

namespace gsst::detail {

// =============================================================================
// GPU Sampling kernel stub
// =============================================================================
// Will be ported from fsst-gpu's gpu_sampling kernel:
// - 1 CUDA block per data block, 128 threads
// - Randomly samples 16KB chunks for symbol table construction
__global__ void sampling_kernel(const uint8_t *src, uint8_t *sample_buf,
                                size_t block_size, size_t total_size) {
  // TODO: Port gpu_sampling from fsst-gpu
  // For now this is a placeholder
  const size_t block_start = static_cast<size_t>(blockIdx.x) * block_size;
  const size_t block_len = min(block_size, total_size - block_start);
  const size_t sample_target = 16384;

  // Simple sequential copy of first sample_target bytes per block
  for (size_t i = threadIdx.x; i < min(block_len, sample_target);
       i += blockDim.x) {
    sample_buf[blockIdx.x * sample_target + i] = src[block_start + i];
  }
}

Status sample_blocks_gpu(const uint8_t *d_src, size_t src_size,
                         size_t block_size, uint8_t *d_samples,
                         uint32_t num_blocks, cudaStream_t stream) {
  const int threads = 128;
  sampling_kernel<<<num_blocks, threads, 0, stream>>>(d_src, d_samples,
                                                      block_size, src_size);

  cudaError_t err = cudaGetLastError();
  if (err != cudaSuccess)
    return Status::ErrorCudaError;

  return Status::Success;
}

// =============================================================================
// GPU Encoding kernel stub
// =============================================================================
// Will be ported from fsst-gpu's compaction_v5t encoding kernel:
// - 128 threads/block, 10240 bytes/thread tile
// - Shared memory encoding table (SmallSymbolMatchTableData)
// - Warp voting for symbol lookups
// - CUB stream compaction
__global__ void encode_kernel(const uint8_t *src, size_t src_size, uint8_t *dst,
                              uint32_t *out_sizes) {
  // TODO: Port encoding kernel from fsst-gpu
}

Status encode_block(const uint8_t *d_src, size_t src_size, uint8_t *d_dst,
                    size_t dst_capacity, const EncoderTable *encoder,
                    size_t *compressed_size, cudaStream_t stream) {
  // TODO: Implement GPU encoding pipeline
  // For now: return error to indicate GPU path not yet available
  (void)d_src;
  (void)src_size;
  (void)d_dst;
  (void)dst_capacity;
  (void)encoder;
  (void)compressed_size;
  (void)stream;
  return Status::ErrorInternal;
}

} // namespace gsst::detail
