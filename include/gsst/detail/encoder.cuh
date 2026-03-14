// =============================================================================
// GSST — Internal: GPU encoder interface
// =============================================================================
// Declares the GPU compression kernel launch interface.
// The actual kernels are defined in encoder.cu.
// =============================================================================
#pragma once

#include "common.hpp"
#include "format.hpp"
#include "symbol_table.hpp"
#include <gsst/gsst.hpp>

namespace gsst::detail {

/// Compress a single block on the GPU using the given encoder table.
///
/// @param d_src            Device pointer to uncompressed block data.
/// @param src_size         Size of the uncompressed block.
/// @param d_dst            Device pointer to output buffer.
/// @param dst_capacity     Capacity of the output buffer.
/// @param encoder          Encoder table (host pointer; will be copied to
/// device).
/// @param compressed_size  [out] Actual compressed size.
/// @param stream           CUDA stream.
/// @return                 Status code.
Status encode_block(const uint8_t *d_src, size_t src_size, uint8_t *d_dst,
                    size_t dst_capacity, const EncoderTable *encoder,
                    size_t *compressed_size, cudaStream_t stream);

/// Sample data from GPU memory to build symbol table.
///
/// @param d_src       Device pointer to input data.
/// @param src_size    Total input size.
/// @param block_size  Size of each block.
/// @param d_samples   Device pointer to sample output buffer.
/// @param num_blocks  Number of blocks to sample.
/// @param stream      CUDA stream.
/// @return            Status code.
Status sample_blocks_gpu(const uint8_t *d_src, size_t src_size,
                         size_t block_size, uint8_t *d_samples,
                         uint32_t num_blocks, cudaStream_t stream);

} // namespace gsst::detail
