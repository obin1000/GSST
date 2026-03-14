// =============================================================================
// GSST — Symbol table tests
// =============================================================================
#include <cstring>
#include <gsst/detail/common.hpp>
#include <gsst/detail/symbol_table.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>


using namespace gsst::detail;

TEST(SymbolTableTest, BuildFromSample) {
  // Create a sample with some repetitive patterns
  std::string sample;
  for (int i = 0; i < 1000; ++i) {
    sample += "hello world ";
  }

  DecoderTable decoder{};
  auto *encoder =
      build_encoder_table(reinterpret_cast<const uint8_t *>(sample.data()),
                          sample.size(), &decoder);

  ASSERT_NE(encoder, nullptr);
  EXPECT_EQ(decoder.version, FORMAT_VERSION);

  // There should be symbols for frequent characters
  bool found_symbol = false;
  for (int i = 0; i < MAX_SYMBOLS; ++i) {
    if (decoder.len[i] > 0) {
      found_symbol = true;
      EXPECT_GE(decoder.len[i], 1);
      EXPECT_LE(decoder.len[i], MAX_SYMBOL_LEN);
      break;
    }
  }
  EXPECT_TRUE(found_symbol);

  free_encoder_table(encoder);
}

TEST(SymbolTableTest, ExportImportRoundtrip) {
  // Build a table
  std::string sample = "aaabbbcccaaabbbcccaaabbbccc";
  DecoderTable original{};
  auto *encoder =
      build_encoder_table(reinterpret_cast<const uint8_t *>(sample.data()),
                          sample.size(), &original);
  ASSERT_NE(encoder, nullptr);

  // Export
  uint8_t buf[2200];
  size_t exported_size = original.export_to(buf, sizeof(buf));
  ASSERT_GT(exported_size, 0u);

  // Import
  DecoderTable imported{};
  size_t imported_size = imported.import_from(buf, exported_size);
  ASSERT_GT(imported_size, 0u);

  // Compare
  EXPECT_EQ(imported.version, original.version);
  EXPECT_EQ(imported.zero_terminated, original.zero_terminated);

  // Count symbols in both tables
  int orig_count = 0, imp_count = 0;
  for (int i = 0; i < MAX_SYMBOLS; ++i) {
    if (original.len[i] > 0)
      orig_count++;
    if (imported.len[i] > 0)
      imp_count++;
  }
  EXPECT_EQ(orig_count, imp_count);

  // The imported table should contain the same symbol data
  // (possibly reordered by length group, which is fine)
  for (int i = 0; i < MAX_SYMBOLS; ++i) {
    if (original.len[i] > 0) {
      // Find this symbol in the imported table
      bool found = false;
      for (int j = 0; j < MAX_SYMBOLS; ++j) {
        if (imported.len[j] == original.len[i]) {
          uint64_t mask = (original.len[i] < 8)
                              ? ((1ULL << (original.len[i] * 8)) - 1)
                              : ~0ULL;
          if ((imported.symbol[j] & mask) == (original.symbol[i] & mask)) {
            found = true;
            break;
          }
        }
      }
      EXPECT_TRUE(found) << "Symbol " << i << " not found after import";
    }
  }

  free_encoder_table(encoder);
}

TEST(SymbolTableTest, NullInputReturnsNull) {
  EXPECT_EQ(build_encoder_table(nullptr, 0, nullptr), nullptr);
  EXPECT_EQ(build_encoder_table(nullptr, 100, nullptr), nullptr);
  uint8_t data[] = {1, 2, 3};
  EXPECT_EQ(build_encoder_table(data, 0, nullptr), nullptr);
}

TEST(DecoderTableTest, SymbolLength) {
  DecoderTable dec{};
  dec.len[0] = 3;
  dec.len[100] = 1;

  EXPECT_EQ(dec.symbol_length(0), 3);
  EXPECT_EQ(dec.symbol_length(100), 1);
  EXPECT_EQ(dec.symbol_length(ESC_CODE), 1); // ESC always emits 1 byte
}
