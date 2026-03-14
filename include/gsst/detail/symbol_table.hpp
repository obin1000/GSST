// =============================================================================
// GSST — Internal: symbol table types
// =============================================================================
// Defines the decoder table (shared between CPU and GPU) and the GPU-optimized
// encoding table structures used during compression.
// =============================================================================
#pragma once

#include "common.hpp"

namespace gsst::detail {

// =============================================================================
// Decoder table — used for decompression on both CPU and GPU
// =============================================================================
// Compatible with the original gsst_decoder / FSST decoder format.
// Each code 0..254 maps to a symbol of 1..8 bytes.
// Code 255 is the escape code: the following byte is emitted literally.

struct DecoderTable {
  uint64_t version;             ///< FORMAT_VERSION
  uint8_t zero_terminated;      ///< Whether the zero byte is a terminator
  uint8_t len[MAX_SYMBOLS];     ///< len[code] = byte-length of symbol
  uint64_t symbol[MAX_SYMBOLS]; ///< symbol[code] = little-endian byte sequence

  /// Import a decoder table from a serialized FSST header buffer.
  /// Returns the number of bytes consumed, or 0 on error.
  size_t import_from(const uint8_t *data, size_t data_len);

  /// Export this decoder table to a serialized FSST header buffer.
  /// Returns the number of bytes written.
  size_t export_to(uint8_t *buf, size_t buf_capacity) const;

  /// Look up symbol length for a given code.
  GSST_HD uint8_t symbol_length(uint8_t code) const {
    return (code == ESC_CODE) ? 1 : len[code];
  }
};

// =============================================================================
// Encoder table — used for compression on GPU
// =============================================================================
// This is the GPU-friendly encoding table. The actual GPU-optimized formats
// (ELL sparse, match table, etc.) are implementation details of the encoder.
//
// For the public interface, we expose the encoder table as an opaque blob
// that is built from sampled input data.

/// Opaque encoding table handle (forward-declared; defined in encoder.cu)
struct EncoderTable;

/// Build an encoder table from sample data.
/// @param sample_data  Pointer to representative sample bytes.
/// @param sample_size  Number of sample bytes.
/// @param decoder_out  If non-null, receives the corresponding decoder table.
/// @return             Heap-allocated encoder table (caller must delete).
EncoderTable *build_encoder_table(const uint8_t *sample_data,
                                  size_t sample_size,
                                  DecoderTable *decoder_out = nullptr);

/// Free an encoder table.
void free_encoder_table(EncoderTable *table);

} // namespace gsst::detail
