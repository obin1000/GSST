// =============================================================================
// GSST — Internal: GPU encoder interface
// =============================================================================
// Declares the GPU compression kernel launch interface.
// The actual kernels are defined in encoder.cu.
// =============================================================================
#pragma once

#include <gsst/gsst.hpp>

#include "common.hpp"
#include "format.hpp"
#include "symbol_table.hpp"


namespace gsst::detail {

// =============================================================================
// GPU Encoder constants
// =============================================================================
inline constexpr int ENC_THREAD_COUNT = 128;
inline constexpr int ENC_TILE_LEN = 10240;                                                      // bytes per thread
inline constexpr size_t ENC_BLOCK_SIZE = static_cast<size_t>(ENC_TILE_LEN) * ENC_THREAD_COUNT;  // 1,310,720 bytes

/// Sample data from GPU memory to build symbol table.
Status sample_blocks_gpu(const uint8_t* d_src, size_t src_size, size_t block_size, uint8_t* d_samples,
                         uint32_t num_blocks, cudaStream_t stream);

}  // namespace gsst::detail
