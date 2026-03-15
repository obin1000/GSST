// =============================================================================
// GSST — Internal: symbol table types
// =============================================================================
// Defines the decoder table (shared between CPU and GPU) and the GPU-optimized
// encoding table structures used during compression.
// =============================================================================
#pragma once

#include <cstring>

#include "common.hpp"


// FSST hash function used for 3+ byte symbol lookup
#ifndef FSST_HASH
#define FSST_HASH(w) (((w) * 2971215073LL) ^ (((w) * 2971215073LL) >> 15))
#endif

namespace gsst::detail {

// =============================================================================
// Decoder table — used for decompression on both CPU and GPU
// =============================================================================
// Compatible with the original gsst_decoder / FSST decoder format.
// Each code 0..254 maps to a symbol of 1..8 bytes.
// Code 255 is the escape code: the following byte is emitted literally.

struct DecoderTable {
    uint64_t version;              ///< FORMAT_VERSION
    uint8_t zero_terminated;       ///< Whether the zero byte is a terminator
    uint8_t len[MAX_SYMBOLS];      ///< len[code] = byte-length of symbol
    uint64_t symbol[MAX_SYMBOLS];  ///< symbol[code] = little-endian byte sequence

    /// Import a decoder table from a serialized FSST header buffer.
    /// Returns the number of bytes consumed, or 0 on error.
    size_t import_from(const uint8_t* data, size_t data_len);

    /// Export this decoder table to a serialized FSST header buffer.
    /// Returns the number of bytes written.
    size_t export_to(uint8_t* buf, size_t buf_capacity) const;

    /// Look up symbol length for a given code.
    GSST_HD uint8_t symbol_length(uint8_t code) const { return (code == ESC_CODE) ? 1 : len[code]; }
};

// =============================================================================
// Encoder table — used for compression
// =============================================================================
// Contains the decoder table (for serialization) plus fast lookup structures
// for encoding. The CPU path uses hash/array lookups; the GPU path will use
// a compact shared-memory-friendly representation.

struct EncoderTable {
    DecoderTable decoder;
    uint16_t num_symbols = 0;

    // --- Fast CPU encoding lookup ---
    uint8_t single_code[256];  ///< byte → code (255 = no match/escape)
    uint8_t pair_code[65536];  ///< first|second<<8 → code (255 = no match)

    struct HashEntry {
        uint64_t masked_val = 0;
        uint8_t code = ESC_CODE;
        uint8_t len = 0;
    };
    HashEntry hash[1024];  ///< 3+ byte symbols

    /// Find the longest matching symbol at cur.
    /// @return code:8 | len:8 (code=255 for escape)
    uint16_t findLongest(const uint8_t* cur, int remaining) const {
        // Try 3+ byte hash match first (longest potential match)
        if (remaining >= 3) {
            uint64_t word = 0;
            int load = remaining < 8 ? remaining : 8;
            std::memcpy(&word, cur, load);
            size_t idx = FSST_HASH(word & 0xFFFFFF) & 1023;
            const auto& h = hash[idx];
            if (h.len >= 3 && h.len <= remaining) {
                uint64_t mask = (h.len < 8) ? ((1ULL << (h.len * 8)) - 1) : ~0ULL;
                if ((word & mask) == h.masked_val)
                    return h.code | (static_cast<uint16_t>(h.len) << 8);
            }
        }
        // Try 2-byte match
        if (remaining >= 2) {
            uint16_t key = cur[0] | (static_cast<uint16_t>(cur[1]) << 8);
            if (pair_code[key] != ESC_CODE)
                return pair_code[key] | (2 << 8);
        }
        // 1-byte lookup
        return single_code[cur[0]] | (1 << 8);
    }
};

/// Build an encoder table from sample data using the full FSST algorithm.
/// @param sample_data  Pointer to representative sample bytes.
/// @param sample_size  Number of sample bytes.
/// @return             Heap-allocated encoder table (caller must free).
EncoderTable* build_encoder_table(const uint8_t* sample_data, size_t sample_size);

/// Free an encoder table.
void free_encoder_table(EncoderTable* table);

}  // namespace gsst::detail
