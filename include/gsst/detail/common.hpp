// =============================================================================
// GSST — Internal: common types, constants, CUDA helpers
// =============================================================================
#pragma once

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <cuda_runtime.h>

namespace gsst::detail {

// =============================================================================
// Constants
// =============================================================================
inline constexpr uint64_t FORMAT_VERSION = 20240131;
inline constexpr uint8_t ESC_CODE = 255;               // FSST escape code
inline constexpr uint8_t PAD_CODE = 254;               // Padding symbol
inline constexpr uint8_t MAX_SYMBOLS = 255;            // Codes 0..254
inline constexpr uint8_t MAX_SYMBOL_LEN = 8;           // Max bytes per symbol
inline constexpr size_t DEFAULT_BLOCK_SIZE = 1u << 20; // 1 MiB per block

// =============================================================================
// CUDA error checking
// =============================================================================

#define GSST_CUDA_CHECK(expr)                                                  \
  do {                                                                         \
    cudaError_t err_ = (expr);                                                 \
    if (err_ != cudaSuccess) {                                                 \
      fprintf(stderr, "CUDA error at %s:%d — %s (%s)\n", __FILE__, __LINE__,   \
              cudaGetErrorString(err_), cudaGetErrorName(err_));               \
      return gsst::Status::ErrorCudaError;                                     \
    }                                                                          \
  } while (0)

#define GSST_CUDA_CHECK_VOID(expr)                                             \
  do {                                                                         \
    cudaError_t err_ = (expr);                                                 \
    if (err_ != cudaSuccess) {                                                 \
      fprintf(stderr, "CUDA error at %s:%d — %s (%s)\n", __FILE__, __LINE__,   \
              cudaGetErrorString(err_), cudaGetErrorName(err_));               \
    }                                                                          \
  } while (0)

// =============================================================================
// Host/device portability
// =============================================================================
#ifdef __CUDACC__
#define GSST_HD __host__ __device__
#else
#define GSST_HD
#endif

// =============================================================================
// Serialization helpers (big-endian, matching gsst-main convention)
// =============================================================================
template <size_t N> GSST_HD inline void serialize(uint64_t val, uint8_t *p) {
  static_assert(N >= 1 && N <= 8);
  for (size_t i = 0; i < N; ++i) {
    p[i] = static_cast<uint8_t>((val >> (8 * (N - 1 - i))) & 0xFF);
  }
}

template <size_t N> GSST_HD inline uint64_t deserialize(const uint8_t *p) {
  static_assert(N >= 1 && N <= 8);
  uint64_t result = 0;
  for (size_t i = 0; i < N; ++i) {
    result = (result << 8) | p[i];
  }
  return result;
}

// =============================================================================
// Aligned pointer helper (for CUDA shared/global memory)
// =============================================================================
template <unsigned Alignment> GSST_HD inline uint8_t *align_up(uint8_t *ptr) {
  auto addr = reinterpret_cast<uintptr_t>(ptr);
  auto aligned =
      (addr + Alignment - 1) & ~(static_cast<uintptr_t>(Alignment) - 1);
  return reinterpret_cast<uint8_t *>(aligned);
}

template <unsigned Alignment>
GSST_HD inline const uint8_t *align_up(const uint8_t *ptr) {
  return const_cast<const uint8_t *>(
      align_up<Alignment>(const_cast<uint8_t *>(ptr)));
}

// =============================================================================
// Block-size computation
// =============================================================================
GSST_HD inline constexpr uint64_t compute_block_size(uint64_t input_size,
                                                     uint32_t num_blocks) {
  return (input_size + num_blocks - 1) / num_blocks;
}

// =============================================================================
// Timing utility
// =============================================================================
struct Timer {
  using clock = std::chrono::high_resolution_clock;
  clock::time_point start_;

  Timer() : start_(clock::now()) {}
  void reset() { start_ = clock::now(); }

  [[nodiscard]] double elapsed_ms() const {
    auto now = clock::now();
    return std::chrono::duration<double, std::milli>(now - start_).count();
  }
};

} // namespace gsst::detail
