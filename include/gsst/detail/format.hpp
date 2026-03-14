// =============================================================================
// GSST — Internal: compressed data format
// =============================================================================
// Defines the on-disk / in-memory binary format for GSST compressed data.
//
// The format is:
//
//   [FileHeader]
//   [DecoderTable × num_tables]          — serialized FSST decoder tables
//   [BlockDescriptor × num_blocks]       — per-block metadata
//   [CompressedBlockData × num_blocks]   — packed compressed payloads
//
// Each block references one decoder table. The layout field in the file header
// determines how the compressed data within each block is organized (Blocks,
// Splits, or Coalesce).
// =============================================================================
#pragma once

#include "common.hpp"

namespace gsst::detail {

// =============================================================================
// File header — at the start of every compressed buffer
// =============================================================================
struct FileHeader {
  // Magic bytes: "GSST" (0x47535354)
  static constexpr uint32_t MAGIC = 0x47535354;

  uint32_t magic;             ///< Must be MAGIC
  uint32_t version;           ///< FORMAT_VERSION
  uint64_t uncompressed_size; ///< Total original data size
  uint64_t
      compressed_size; ///< Total compressed buffer size (including this header)
  uint32_t num_tables; ///< Number of distinct symbol tables
  uint32_t num_blocks; ///< Number of compressed blocks
  uint32_t table_section_size; ///< Byte size of the table section
  uint32_t splits_per_block;   ///< For Splits/Coalesce layouts (0 for Blocks)
  uint8_t layout;              ///< Layout enum value
  uint8_t reserved[7];         ///< Padding / future use

  /// Validate this header.
  [[nodiscard]] bool is_valid() const noexcept {
    return magic == MAGIC && version == static_cast<uint32_t>(FORMAT_VERSION);
  }
};

static_assert(sizeof(FileHeader) == 48, "FileHeader must be 48 bytes");

// =============================================================================
// Block descriptor — one per compressed block
// =============================================================================
struct BlockDescriptor {
  uint32_t compressed_size;   ///< Size of this block's compressed payload
  uint32_t uncompressed_size; ///< Size of original data in this block
  uint32_t table_index;       ///< Index into the table section
  uint32_t num_splits;        ///< Number of splits (1 for Blocks layout)
};

static_assert(sizeof(BlockDescriptor) == 16,
              "BlockDescriptor must be 16 bytes");

// =============================================================================
// Format read/write helpers
// =============================================================================

/// Write a FileHeader to a buffer. Returns bytes written.
size_t write_file_header(uint8_t *dst, const FileHeader &header);

/// Read a FileHeader from a buffer. Returns true on success.
bool read_file_header(const uint8_t *src, size_t src_size, FileHeader &header);

/// Compute the offset to the table section.
inline constexpr size_t table_section_offset() { return sizeof(FileHeader); }

/// Compute the offset to the block descriptor array.
inline size_t block_descriptors_offset(const FileHeader &hdr) {
  return sizeof(FileHeader) + hdr.table_section_size;
}

/// Compute the offset to the compressed data payloads.
inline size_t data_section_offset(const FileHeader &hdr) {
  return block_descriptors_offset(hdr) +
         static_cast<size_t>(hdr.num_blocks) * sizeof(BlockDescriptor);
}

} // namespace gsst::detail
