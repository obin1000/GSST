// =============================================================================
// GSST — GPU Static Symbol Table Compression
// =============================================================================
// Public C++ header — include this file to use the library.
//
// GSST compresses and decompresses byte-oriented data (typically strings) on
// NVIDIA GPUs using the FSST (Fast Static Symbol Table) algorithm. It achieves
// 74+ GB/s compression throughput on modern GPUs with competitive compression
// ratios.
//
// Quick start:
//
//   #include <gsst/gsst.hpp>
//
//   gsst::Codec codec;
//
//   // Compress (host buffers)
//   gsst::CompressResult cr = codec.compress(src, src_size, dst, dst_capacity);
//
//   // Decompress (host buffers)
//   gsst::DecompressResult dr = codec.decompress(compressed, comp_size, out,
//   out_capacity);
//
//   // Or use device buffers directly:
//   gsst::CompressResult cr = codec.compress_device(d_src, src_size, d_dst,
//   dst_capacity, stream);
//
// =============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Forward-declare CUDA types so users don't need cuda_runtime.h in host code
#ifndef __CUDACC__
using cudaStream_t = struct CUstream_st *;
#else
#include <cuda_runtime.h>
#endif

namespace gsst {

// =============================================================================
// Version
// =============================================================================
inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

/// Wire-format version for encoded data (shared with original implementations)
inline constexpr uint64_t FORMAT_VERSION = 20240131;

// =============================================================================
// Status codes
// =============================================================================
enum class Status : int {
  Success = 0,
  ErrorInvalidArgument,   ///< Null pointer, zero size, etc.
  ErrorBufferTooSmall,    ///< Output buffer insufficient
  ErrorBadAlignment,      ///< Buffer alignment requirement not met
  ErrorCudaError,         ///< A CUDA API call failed
  ErrorCorruptData,       ///< Input data is corrupt or not GSST-encoded
  ErrorCorruptHeader,     ///< File/block header is invalid
  ErrorUnsupportedFormat, ///< Format version not recognized
  ErrorInternal,          ///< Unexpected internal error
};

/// Human-readable description of a status code.
[[nodiscard]] const char *status_string(Status s) noexcept;

// =============================================================================
// Compressed data layouts
// =============================================================================
/// The decompression strategy / data layout used within compressed blocks.
enum class Layout : uint8_t {
  /// One FSST table per block; each GPU thread decompresses a full block.
  Blocks = 0,

  /// Blocks are subdivided into splits with recorded start offsets, enabling
  /// multiple threads to decompress each block in parallel.
  Splits = 1,

  /// Symbols are interleaved across splits so that GPU threads reading
  /// consecutive addresses achieve coalesced global-memory access.
  Coalesce = 2,
};

// =============================================================================
// Configuration
// =============================================================================

/// Parameters controlling compression behavior.
struct CompressOptions {
  /// Data layout to produce (affects decompression parallelism).
  Layout layout = Layout::Splits;

  /// Number of independent blocks the input is divided into.
  /// More blocks = more parallelism but slightly worse compression ratio.
  /// 0 = automatic (based on input size and GPU capability).
  uint32_t num_blocks = 0;

  /// For Splits / Coalesce layouts: sub-divisions per block.
  /// 0 = automatic.
  uint32_t splits_per_block = 0;

  /// CUDA stream for GPU operations. nullptr = default stream.
  cudaStream_t stream = nullptr;
};

/// Parameters controlling decompression behavior.
struct DecompressOptions {
  /// CUDA stream for GPU operations. nullptr = default stream.
  cudaStream_t stream = nullptr;

  /// Threads per CUDA block for GPU decompression kernels.
  /// 0 = automatic (kernel-specific default).
  uint32_t threads_per_block = 0;

  /// Number of CUDA blocks to launch.
  /// 0 = automatic.
  uint32_t num_cuda_blocks = 0;
};

// =============================================================================
// Results
// =============================================================================

struct CompressResult {
  Status status = Status::Success;
  size_t compressed_size = 0;   ///< Bytes written to the output buffer.
  size_t uncompressed_size = 0; ///< Original input size (for reference).
};

struct DecompressResult {
  Status status = Status::Success;
  size_t decompressed_size = 0; ///< Bytes written to the output buffer.
};

// =============================================================================
// Utility — buffer size queries
// =============================================================================

/// Upper bound on compressed output size for a given input size.
/// Useful for pre-allocating the destination buffer.
[[nodiscard]] size_t compress_bound(size_t input_size,
                                    const CompressOptions &opts = {}) noexcept;

/// Returns the decompressed (original) size stored in a compressed buffer's
/// header, without performing actual decompression.  Returns 0 on error.
[[nodiscard]] size_t get_decompressed_size(const uint8_t *compressed_data,
                                           size_t compressed_size) noexcept;

// =============================================================================
// Core Codec class
// =============================================================================

/// Stateless codec providing compress / decompress operations.
///
/// Thread-safety: a single Codec instance may be used from multiple host
/// threads provided that each call uses a distinct CUDA stream.
class Codec {
public:
  Codec();
  ~Codec();

  Codec(const Codec &) = delete;
  Codec &operator=(const Codec &) = delete;
  Codec(Codec &&) noexcept;
  Codec &operator=(Codec &&) noexcept;

  // ----- Host-memory API --------------------------------------------------

  /// Compress data residing in host (CPU) memory.
  /// @param src        Pointer to uncompressed input data.
  /// @param src_size   Size of input data in bytes.
  /// @param dst        Pointer to output buffer (host memory).
  /// @param dst_capacity  Size of the output buffer in bytes.
  /// @param opts       Compression options.
  /// @return           Result with status and compressed size.
  [[nodiscard]]
  CompressResult compress(const uint8_t *src, size_t src_size, uint8_t *dst,
                          size_t dst_capacity,
                          const CompressOptions &opts = {}) const;

  /// Decompress data residing in host (CPU) memory.
  /// @param src        Pointer to compressed data (host memory).
  /// @param src_size   Size of compressed data in bytes.
  /// @param dst        Pointer to output buffer (host memory).
  /// @param dst_capacity  Size of the output buffer in bytes.
  /// @param opts       Decompression options.
  /// @return           Result with status and decompressed size.
  [[nodiscard]]
  DecompressResult decompress(const uint8_t *src, size_t src_size, uint8_t *dst,
                              size_t dst_capacity,
                              const DecompressOptions &opts = {}) const;

  // ----- Device-memory API ------------------------------------------------

  /// Compress data already residing in GPU device memory.
  [[nodiscard]]
  CompressResult compress_device(const uint8_t *d_src, size_t src_size,
                                 uint8_t *d_dst, size_t dst_capacity,
                                 const CompressOptions &opts = {}) const;

  /// Decompress data already residing in GPU device memory.
  [[nodiscard]]
  DecompressResult decompress_device(const uint8_t *d_src, size_t src_size,
                                     uint8_t *d_dst, size_t dst_capacity,
                                     const DecompressOptions &opts = {}) const;

  // ----- Convenience wrappers --------------------------------------------

  /// Compress a std::string / string_view. Returns compressed bytes in a
  /// vector, or an empty vector on failure.
  [[nodiscard]]
  std::vector<uint8_t> compress(std::string_view input,
                                const CompressOptions &opts = {}) const;

  /// Decompress into a std::string. Returns empty string on failure.
  [[nodiscard]]
  std::string decompress_string(const uint8_t *src, size_t src_size,
                                const DecompressOptions &opts = {}) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace gsst
