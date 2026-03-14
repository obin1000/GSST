// =============================================================================
// GSST — Symbol table construction
// =============================================================================
// Builds FSST symbol tables from sample data. This is a reference CPU
// implementation mirroring the algorithm from the FSST paper / fsst-gpu.
//
// The GPU-accelerated sampling kernel is stubbed here; the actual kernel will
// be ported from fsst-gpu's gpu_sampling and the modified FSST table builder.
// =============================================================================

#include <gsst/detail/common.hpp>
#include <gsst/detail/symbol_table.hpp>


#include <algorithm>
#include <cstring>
#include <numeric>
#include <queue>
#include <vector>

namespace gsst::detail {

// =============================================================================
// DecoderTable import/export  (FSST-compatible serialization)
// =============================================================================
// The serialized format matches the original FSST:
//   version      : 8 bytes (little-endian)
//   zeroTerminated: 1 byte
//   lenHisto     : 8 bytes (histogram of symbol lengths)
//   symbols      : variable (concatenated symbol bytes grouped by length)
//
// Total max size: 8 + 1 + 8 + 255*8 = 2057 bytes

size_t DecoderTable::import_from(const uint8_t *data, size_t data_len) {
  if (data_len < 17)
    return 0; // version(8) + zeroTerminated(1) + lenHisto(8)

  size_t pos = 0;

  // Version (little-endian)
  std::memcpy(&version, data + pos, 8);
  pos += 8;

  zero_terminated = data[pos++];

  // Length histogram: lenHisto[i] = count of symbols with length (i+1)
  uint8_t len_histo[8];
  std::memcpy(len_histo, data + pos, 8);
  pos += 8;

  // Zero out tables
  std::memset(len, 0, sizeof(len));
  std::memset(symbol, 0, sizeof(symbol));

  // Read symbols grouped by length
  uint16_t code = 0;
  for (int l = 0; l < 8; ++l) {
    const uint8_t sym_len = static_cast<uint8_t>(l + 1);
    for (int s = 0; s < len_histo[l]; ++s) {
      if (code >= MAX_SYMBOLS)
        return 0;
      if (pos + sym_len > data_len)
        return 0;

      len[code] = sym_len;
      uint64_t sym = 0;
      std::memcpy(&sym, data + pos, sym_len); // little-endian
      symbol[code] = sym;
      pos += sym_len;
      code++;
    }
  }

  // Remaining codes get length 0 (unused)
  return pos;
}

size_t DecoderTable::export_to(uint8_t *buf, size_t buf_capacity) const {
  if (buf_capacity < 17)
    return 0;

  size_t pos = 0;

  // Version (little-endian)
  std::memcpy(buf + pos, &version, 8);
  pos += 8;

  buf[pos++] = zero_terminated;

  // Build length histogram and sorted code ordering
  uint8_t len_histo[8] = {};
  uint16_t codes_by_length[8][256]; // codes_by_length[len-1][i]
  uint16_t counts[8] = {};

  for (uint16_t c = 0; c < MAX_SYMBOLS; ++c) {
    if (len[c] > 0 && len[c] <= 8) {
      const int idx = len[c] - 1;
      len_histo[idx]++;
      codes_by_length[idx][counts[idx]++] = c;
    }
  }

  std::memcpy(buf + pos, len_histo, 8);
  pos += 8;

  // Write symbols grouped by length
  for (int l = 0; l < 8; ++l) {
    const uint8_t sym_len = static_cast<uint8_t>(l + 1);
    for (int s = 0; s < counts[l]; ++s) {
      const uint16_t c = codes_by_length[l][s];
      if (pos + sym_len > buf_capacity)
        return 0;
      std::memcpy(buf + pos, &symbol[c], sym_len);
      pos += sym_len;
    }
  }

  return pos;
}

// =============================================================================
// EncoderTable — opaque structure wrapping the decoder for CPU encoding
// =============================================================================
// In the full GPU implementation, this would contain the GPU-optimized
// ELL/Match table. For the CPU reference path, we just wrap the decoder.

struct EncoderTable {
  DecoderTable decoder;
  // TODO: Add GPU-specific encoding table (SmallSymbolMatchTableData, etc.)
};

// =============================================================================
// Simple FSST table builder (CPU reference)
// =============================================================================
// This implements a simplified version of the FSST table construction:
// 1. Count byte and 2-gram frequencies
// 2. Greedily pick highest-gain symbols
// 3. Iterate to refine
//
// The full GPU-accelerated version (from fsst-gpu) with sampling kernels,
// ELL tables, and match tables will be integrated later.

namespace {

struct Candidate {
  uint64_t bytes; // little-endian symbol bytes
  uint8_t length;
  int64_t gain; // estimated compression gain
};

void count_frequencies(const uint8_t *data, size_t len, uint64_t *freq1,
                       uint64_t freq2[256][256]) {
  for (size_t i = 0; i < len; ++i) {
    freq1[data[i]]++;
    if (i + 1 < len)
      freq2[data[i]][data[i + 1]]++;
  }
}

} // anonymous namespace

EncoderTable *build_encoder_table(const uint8_t *sample_data,
                                  size_t sample_size,
                                  DecoderTable *decoder_out) {
  if (!sample_data || sample_size == 0)
    return nullptr;

  auto *table = new EncoderTable{};
  auto &dec = table->decoder;

  dec.version = FORMAT_VERSION;
  dec.zero_terminated = 0;
  std::memset(dec.len, 0, sizeof(dec.len));
  std::memset(dec.symbol, 0, sizeof(dec.symbol));

  // --- Frequency counting ------------------------------------------------
  uint64_t freq1[256] = {};
  uint64_t freq2[256][256] = {};
  count_frequencies(sample_data, sample_size, freq1,
                    reinterpret_cast<uint64_t *>(freq2));

  // --- Build candidate list: all 1-byte and frequent 2-byte symbols ------
  std::vector<Candidate> candidates;
  candidates.reserve(256 + 256 * 256);

  // All single bytes
  for (int b = 0; b < 256; ++b) {
    if (freq1[b] > 0) {
      candidates.push_back({
          .bytes = static_cast<uint64_t>(b),
          .length = 1,
          .gain = static_cast<int64_t>(freq1[b]),
      });
    }
  }

  // Frequent 2-byte pairs
  for (int b1 = 0; b1 < 256; ++b1) {
    for (int b2 = 0; b2 < 256; ++b2) {
      if (freq2[b1][b2] > 1) {
        uint64_t sym =
            static_cast<uint64_t>(b1) | (static_cast<uint64_t>(b2) << 8);
        // Gain: each occurrence saves 1 byte (2→1 in output)
        int64_t gain = static_cast<int64_t>(freq2[b1][b2]);
        candidates.push_back({
            .bytes = sym,
            .length = 2,
            .gain = gain,
        });
      }
    }
  }

  // --- Sort by gain descending and pick top 254 symbols ------------------
  // (Code 255 reserved for ESC)
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b) {
              if (a.gain != b.gain)
                return a.gain > b.gain;
              return a.length > b.length; // prefer longer symbols at same gain
            });

  uint16_t num_symbols = 0;
  for (const auto &cand : candidates) {
    if (num_symbols >= MAX_SYMBOLS)
      break;
    dec.len[num_symbols] = cand.length;
    dec.symbol[num_symbols] = cand.bytes;
    num_symbols++;
  }

  if (decoder_out) {
    *decoder_out = dec;
  }

  return table;
}

void free_encoder_table(EncoderTable *table) { delete table; }

} // namespace gsst::detail
