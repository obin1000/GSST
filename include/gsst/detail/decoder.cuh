// =============================================================================
// GSST — Internal: GPU decoder interface
// =============================================================================
// Declares the GPU decompression kernel launch interface.
// Supports Blocks, Splits, and Coalesce layouts.
// =============================================================================
#pragma once

#include "common.hpp"
#include "format.hpp"
#include "symbol_table.hpp"
#include <gsst/gsst.hpp>

namespace gsst::detail {

/// Decompress blocks using the simple Blocks layout.
/// Each GPU thread decompresses an entire block.
///
/// @param d_src              Device pointer to the compressed block data.
/// @param block_descriptors  Host-side array of block descriptors.
/// @param decoder            Decoder table (host pointer; copied to device).
/// @param num_blocks         Number of blocks.
/// @param d_dst              Device pointer to output buffer.
/// @param dst_capacity       Capacity of the output buffer.
/// @param decompressed_size  [out] Actual decompressed size.
/// @param stream             CUDA stream.
/// @param threads_per_block  Threads per CUDA block (0 = auto).
/// @param num_cuda_blocks    CUDA grid size (0 = auto).
/// @return                   Status code.
Status decode_blocks(const uint8_t *d_src,
                     const BlockDescriptor *block_descriptors,
                     const DecoderTable *decoder, uint32_t num_blocks,
                     uint8_t *d_dst, size_t dst_capacity,
                     size_t *decompressed_size, cudaStream_t stream,
                     uint32_t threads_per_block, uint32_t num_cuda_blocks);

/// Decompress blocks using the Splits layout.
/// Multiple threads collaborate on each block via sub-block start offsets.
Status decode_splits(const uint8_t *d_src,
                     const BlockDescriptor *block_descriptors,
                     const DecoderTable *decoder, uint32_t num_blocks,
                     uint8_t *d_dst, size_t dst_capacity,
                     size_t *decompressed_size, cudaStream_t stream,
                     uint32_t threads_per_block, uint32_t num_cuda_blocks);

/// Decompress blocks using the Coalesce (interleaved) layout.
/// Threads read interleaved symbols for coalesced memory access.
Status decode_coalesce(const uint8_t *d_src,
                       const BlockDescriptor *block_descriptors,
                       const DecoderTable *decoder, uint32_t num_blocks,
                       uint8_t *d_dst, size_t dst_capacity,
                       size_t *decompressed_size, cudaStream_t stream,
                       uint32_t threads_per_block, uint32_t num_cuda_blocks);

} // namespace gsst::detail
