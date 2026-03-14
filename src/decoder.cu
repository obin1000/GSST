// =============================================================================
// GSST — GPU Decoder (decompression kernels)
// =============================================================================
// This file contains the GPU decompression kernels for all three layouts:
//   - Blocks:   1 thread per block, simple sequential decode
//   - Splits:   Multiple threads per block via sub-block start offsets
//   - Coalesce: Interleaved symbols for coalesced memory access
//
// These will be ported from gsst-main's CUDA kernels:
//   - blocks_decompress_kernel.cu
//   - splits_decompress_kernel.cu
//   - coalesce_decompress_kernel.cu
// =============================================================================

#include <gsst/detail/common.hpp>
#include <gsst/detail/decoder.cuh>


#include <cuda_runtime.h>

namespace gsst::detail {

// =============================================================================
// Blocks layout — simple kernel (1 thread = 1 block)
// =============================================================================
// Ported from gsst-main's fsst_decompress kernel.
// Each thread:
//   1. Reads the decoder table from constant/shared memory
//   2. Walks the compressed stream byte by byte
//   3. For code 255 (ESC): emits the next literal byte
//   4. For other codes: emits the symbol bytes

__global__ void __launch_bounds__(128)
    decode_blocks_kernel(const uint8_t *__restrict__ src,
                         const uint32_t *__restrict__ compressed_offsets,
                         const uint32_t *__restrict__ uncompressed_offsets,
                         const uint8_t *__restrict__ symbol_lengths,
                         const uint64_t *__restrict__ symbol_values,
                         uint32_t num_blocks, uint8_t *__restrict__ dst) {
  const uint32_t tid = blockIdx.x * blockDim.x + threadIdx.x;

  for (uint32_t block_id = tid; block_id < num_blocks;
       block_id += blockDim.x * gridDim.x) {
    const uint8_t *block_src = src + compressed_offsets[block_id];
    uint8_t *block_dst = dst + uncompressed_offsets[block_id];
    const uint32_t comp_size =
        compressed_offsets[block_id + 1] - compressed_offsets[block_id];

    size_t in_pos = 0;
    size_t out_pos = 0;

    while (in_pos < comp_size) {
      const uint8_t code = block_src[in_pos++];
      if (code == ESC_CODE) {
        block_dst[out_pos++] = block_src[in_pos++];
      } else {
        const uint8_t len = symbol_lengths[code];
        const uint64_t sym = symbol_values[code];
        // Copy symbol bytes (little-endian, up to 8 bytes)
        for (uint8_t b = 0; b < len; ++b) {
          block_dst[out_pos++] = static_cast<uint8_t>((sym >> (b * 8)) & 0xFF);
        }
      }
    }
  }
}

Status decode_blocks(const uint8_t *d_src,
                     const BlockDescriptor *block_descriptors,
                     const DecoderTable *decoder, uint32_t num_blocks,
                     uint8_t *d_dst, size_t dst_capacity,
                     size_t *decompressed_size, cudaStream_t stream,
                     uint32_t threads_per_block, uint32_t num_cuda_blocks) {
  if (threads_per_block == 0)
    threads_per_block = 128;
  if (num_cuda_blocks == 0) {
    num_cuda_blocks = (num_blocks + threads_per_block - 1) / threads_per_block;
    if (num_cuda_blocks == 0)
      num_cuda_blocks = 1;
  }

  // Build prefix sums for compressed/uncompressed offsets
  std::vector<uint32_t> comp_offsets(num_blocks + 1, 0);
  std::vector<uint32_t> uncomp_offsets(num_blocks + 1, 0);
  for (uint32_t i = 0; i < num_blocks; ++i) {
    comp_offsets[i + 1] =
        comp_offsets[i] + block_descriptors[i].compressed_size;
    uncomp_offsets[i + 1] =
        uncomp_offsets[i] + block_descriptors[i].uncompressed_size;
  }

  // Transfer offset arrays to device
  uint32_t *d_comp_offsets = nullptr;
  uint32_t *d_uncomp_offsets = nullptr;
  uint8_t *d_lengths = nullptr;
  uint64_t *d_symbols = nullptr;

  GSST_CUDA_CHECK(cudaMallocAsync(&d_comp_offsets,
                                  (num_blocks + 1) * sizeof(uint32_t), stream));
  GSST_CUDA_CHECK(cudaMallocAsync(&d_uncomp_offsets,
                                  (num_blocks + 1) * sizeof(uint32_t), stream));
  GSST_CUDA_CHECK(
      cudaMallocAsync(&d_lengths, MAX_SYMBOLS * sizeof(uint8_t), stream));
  GSST_CUDA_CHECK(
      cudaMallocAsync(&d_symbols, MAX_SYMBOLS * sizeof(uint64_t), stream));

  GSST_CUDA_CHECK(cudaMemcpyAsync(d_comp_offsets, comp_offsets.data(),
                                  (num_blocks + 1) * sizeof(uint32_t),
                                  cudaMemcpyHostToDevice, stream));
  GSST_CUDA_CHECK(cudaMemcpyAsync(d_uncomp_offsets, uncomp_offsets.data(),
                                  (num_blocks + 1) * sizeof(uint32_t),
                                  cudaMemcpyHostToDevice, stream));
  GSST_CUDA_CHECK(cudaMemcpyAsync(d_lengths, decoder->len,
                                  MAX_SYMBOLS * sizeof(uint8_t),
                                  cudaMemcpyHostToDevice, stream));
  GSST_CUDA_CHECK(cudaMemcpyAsync(d_symbols, decoder->symbol,
                                  MAX_SYMBOLS * sizeof(uint64_t),
                                  cudaMemcpyHostToDevice, stream));

  decode_blocks_kernel<<<num_cuda_blocks, threads_per_block, 0, stream>>>(
      d_src, d_comp_offsets, d_uncomp_offsets, d_lengths, d_symbols, num_blocks,
      d_dst);

  GSST_CUDA_CHECK(cudaGetLastError());
  GSST_CUDA_CHECK(cudaStreamSynchronize(stream));

  if (decompressed_size) {
    *decompressed_size = uncomp_offsets[num_blocks];
  }

  cudaFreeAsync(d_comp_offsets, stream);
  cudaFreeAsync(d_uncomp_offsets, stream);
  cudaFreeAsync(d_lengths, stream);
  cudaFreeAsync(d_symbols, stream);

  return Status::Success;
}

// =============================================================================
// Splits layout — stub kernel
// =============================================================================
// Will be ported from gsst-main's decompress_split_aligned kernel.
// Uses shared-memory double-buffering for aligned reads/writes.

Status decode_splits(const uint8_t *d_src,
                     const BlockDescriptor *block_descriptors,
                     const DecoderTable *decoder, uint32_t num_blocks,
                     uint8_t *d_dst, size_t dst_capacity,
                     size_t *decompressed_size, cudaStream_t stream,
                     uint32_t threads_per_block, uint32_t num_cuda_blocks) {
  // TODO: Port splits decompression kernel from gsst-main
  // For now, fall back to blocks decoder
  return decode_blocks(d_src, block_descriptors, decoder, num_blocks, d_dst,
                       dst_capacity, decompressed_size, stream,
                       threads_per_block, num_cuda_blocks);
}

// =============================================================================
// Coalesce layout — stub kernel
// =============================================================================
// Will be ported from gsst-main's decompress_block kernel.
// Uses cooperative_groups and cuda::memcpy_async for shared memory loads.

Status decode_coalesce(const uint8_t *d_src,
                       const BlockDescriptor *block_descriptors,
                       const DecoderTable *decoder, uint32_t num_blocks,
                       uint8_t *d_dst, size_t dst_capacity,
                       size_t *decompressed_size, cudaStream_t stream,
                       uint32_t threads_per_block, uint32_t num_cuda_blocks) {
  // TODO: Port coalesce decompression kernel from gsst-main
  // For now, fall back to blocks decoder
  return decode_blocks(d_src, block_descriptors, decoder, num_blocks, d_dst,
                       dst_capacity, decompressed_size, stream,
                       threads_per_block, num_cuda_blocks);
}

} // namespace gsst::detail
