// =============================================================================
// GSST — Compression quality and encoder tests
// =============================================================================
// Tests for the FSST-based compression algorithm: symbol table quality,
// compression ratios, encoding correctness, and edge cases.
// =============================================================================
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gsst/detail/common.hpp>
#include <gsst/detail/symbol_table.hpp>
#include <gsst/gsst.hpp>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

using namespace gsst::detail;

// =============================================================================
// Helpers
// =============================================================================

static std::vector<uint8_t> make_repetitive(const std::string& pattern, size_t target_size) {
    std::vector<uint8_t> data;
    data.reserve(target_size);
    while (data.size() < target_size) {
        const size_t chunk = std::min(pattern.size(), target_size - data.size());
        data.insert(data.end(), pattern.begin(), pattern.begin() + static_cast<std::ptrdiff_t>(chunk));
    }
    return data;
}

static std::vector<uint8_t> make_random(size_t size, int alphabet = 256, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, alphabet - 1);
    std::vector<uint8_t> data(size);
    std::ranges::generate(data, [&]() { return static_cast<uint8_t>(dist(rng)); });
    return data;
}

static double compression_ratio(size_t original, size_t compressed) {
    return static_cast<double>(original) / static_cast<double>(compressed);
}

// Helper: compress and return ratio
static double compress_and_ratio(const gsst::Codec& codec, const std::vector<uint8_t>& data) {
    const size_t bound = gsst::compress_bound(data.size());
    std::vector<uint8_t> compressed(bound);
    auto cr = codec.compress(data.data(), data.size(), compressed.data(), compressed.size());
    if (cr.status != gsst::Status::Success) {
        return 0.0;
    }
    return compression_ratio(data.size(), cr.compressed_size);
}

// =============================================================================
// Symbol table quality tests
// =============================================================================

TEST(EncoderTableTest, MultiByteSymbolsPresent) {
    // With highly repetitive data, the FSST algorithm should find multi-byte
    // symbols (not just single-byte passthrough)
    const std::string pattern = "aaaaabbbbbccccc";
    auto data = make_repetitive(pattern, 100000);

    auto* encoder = build_encoder_table(data.data(), data.size());
    ASSERT_NE(encoder, nullptr);

    int multi_byte_count = 0;
    for (uint16_t i = 0; i < encoder->num_symbols; ++i) {
        if (encoder->decoder.len[i] >= 2) {
            multi_byte_count++;
        }
    }
    EXPECT_GT(multi_byte_count, 0) << "FSST should find multi-byte symbols in repetitive data";

    free_encoder_table(encoder);
}

TEST(EncoderTableTest, SymbolTablePopulated) {
    auto data = make_repetitive("The quick brown fox jumps over the lazy dog. Pack my box with five "
                                "dozen liquor jugs. How vexingly quick daft zebras jump! ",
                                50000);

    auto* encoder = build_encoder_table(data.data(), data.size());
    ASSERT_NE(encoder, nullptr);

    // Rich English text with many distinct bigrams should produce many symbols
    EXPECT_GT(encoder->num_symbols, 10) << "Should have more than 10 symbols for varied English text";
    EXPECT_LE(encoder->num_symbols, MAX_SYMBOLS);

    // All symbols should have valid lengths
    for (uint16_t i = 0; i < encoder->num_symbols; ++i) {
        EXPECT_GE(encoder->decoder.len[i], 1);
        EXPECT_LE(encoder->decoder.len[i], MAX_SYMBOL_LEN);
    }

    free_encoder_table(encoder);
}

TEST(EncoderTableTest, SingleByteCodesPopulated) {
    // After building, single_code lookup should have entries for frequent bytes
    std::vector<uint8_t> data(10000, 'X');

    auto* encoder = build_encoder_table(data.data(), data.size());
    ASSERT_NE(encoder, nullptr);

    // 'X' should definitely have a single-byte code
    EXPECT_NE(encoder->single_code['X'], ESC_CODE) << "Frequent byte 'X' should have a code assigned";

    free_encoder_table(encoder);
}

TEST(EncoderTableTest, FindLongestMatchesCorrectly) {
    auto data = make_repetitive("abcabc", 10000);

    auto* encoder = build_encoder_table(data.data(), data.size());
    ASSERT_NE(encoder, nullptr);

    // findLongest should return a valid code for the start of our pattern
    const uint8_t test[] = "abcabc";
    const uint16_t match = encoder->findLongest(test, 6);
    const auto code = static_cast<uint8_t>(match);
    const auto len = static_cast<uint8_t>(match >> 8);

    EXPECT_NE(code, ESC_CODE) << "Should find a symbol match in trained data";
    EXPECT_GE(len, 1);
    EXPECT_LE(len, 8);

    free_encoder_table(encoder);
}

TEST(EncoderTableTest, EscapeForUnseenByte) {
    // Build table on data that never contains byte 0x01
    std::vector<uint8_t> data(10000, 'A');

    auto* encoder = build_encoder_table(data.data(), data.size());
    ASSERT_NE(encoder, nullptr);

    // The FSST algorithm assigns codes to all 256 byte values as single-byte
    // symbols, so 0x01 should still get a code. But if it doesn't (unusual case),
    // findLongest should return ESC_CODE.
    // Check that findLongest always returns a valid result
    const uint8_t test_byte = 0x01;
    const uint16_t match = encoder->findLongest(&test_byte, 1);
    const auto len = static_cast<uint8_t>(match >> 8);
    EXPECT_EQ(len, 1) << "Single byte lookup should always consume 1 byte";

    free_encoder_table(encoder);
}

// =============================================================================
// Compression ratio tests
// =============================================================================

class CompressionRatioTest : public ::testing::Test {
protected:
    gsst::Codec codec;
};

TEST_F(CompressionRatioTest, RepetitiveTextCompressesWell) {
    auto data = make_repetitive("The quick brown fox jumps over the lazy dog. ",
                                1 << 20);  // 1 MiB
    const double ratio = compress_and_ratio(codec, data);
    // FSST on repetitive English text should achieve at least 2x compression
    EXPECT_GT(ratio, 2.0) << "Ratio: " << ratio << " — repetitive text should compress > 2x";
}

TEST_F(CompressionRatioTest, LowEntropyCompressesWell) {
    auto data = make_random(1 << 20, 4);  // Only 4 distinct values
    const double ratio = compress_and_ratio(codec, data);
    // Low-entropy data should compress reasonably well
    EXPECT_GT(ratio, 1.2) << "Ratio: " << ratio << " — low-entropy data should compress > 1.2x";
}

TEST_F(CompressionRatioTest, HighEntropyIsNear1x) {
    auto data = make_random(1 << 20, 256);  // Full random
    const double ratio = compress_and_ratio(codec, data);
    // Random data shouldn't compress much, but should be close to 1x
    // (actually slightly less than 1x due to header + escapes)
    EXPECT_GT(ratio, 0.4) << "Ratio: " << ratio << " — even random data shouldn't blow up too much";
    EXPECT_LT(ratio, 1.5) << "Ratio: " << ratio << " — random data can't compress well";
}

TEST_F(CompressionRatioTest, AllSameByte) {
    const std::vector<uint8_t> data(1 << 20, 'Z');
    const double ratio = compress_and_ratio(codec, data);
    // Uniform data is maximally compressible by FSST (8:1 with 8-byte symbols)
    EXPECT_GT(ratio, 4.0) << "Ratio: " << ratio << " — uniform data should compress > 4x";
}

TEST_F(CompressionRatioTest, SmallInputStillCompresses) {
    // Even small inputs should work (though ratio may be poor due to header)
    std::string text = "hello world hello world hello world";
    auto data = std::vector<uint8_t>(text.begin(), text.end());
    const size_t bound = gsst::compress_bound(data.size());
    std::vector<uint8_t> compressed(bound);
    auto cr = codec.compress(data.data(), data.size(), compressed.data(), compressed.size());
    EXPECT_EQ(cr.status, gsst::Status::Success);
    EXPECT_GT(cr.compressed_size, 0u);
}

TEST_F(CompressionRatioTest, VaryingBlockCountsAffectRatio) {
    auto data = make_repetitive("block count test pattern. ", 1 << 20);

    gsst::CompressOptions opts1;
    gsst::CompressOptions opts4;
    gsst::CompressOptions opts16;
    opts1.num_blocks = 1;
    opts4.num_blocks = 4;
    opts16.num_blocks = 16;

    auto compress_with = [&](const gsst::CompressOptions& opts) -> size_t {
        const size_t bound = gsst::compress_bound(data.size(), opts);
        std::vector<uint8_t> compressed(bound);
        auto cr = codec.compress(data.data(), data.size(), compressed.data(), compressed.size(), opts);
        EXPECT_EQ(cr.status, gsst::Status::Success);
        return cr.compressed_size;
    };

    const size_t size1 = compress_with(opts1);
    const size_t size4 = compress_with(opts4);
    const size_t size16 = compress_with(opts16);

    // More blocks = slightly more overhead (block descriptors)
    // but compressed data should be similar since same table is used
    EXPECT_GT(size1, 0u);
    EXPECT_GT(size4, 0u);
    EXPECT_GT(size16, 0u);

    // 16 blocks should have slightly more overhead than 1 block
    // (16 * 16B descriptors = 256B vs 1 * 16B = 16B)
    // This is a sanity check, not a strict requirement
    EXPECT_LE(size4, size1 + 1024);
    EXPECT_LE(size16, size1 + 4096);
}

// =============================================================================
// Roundtrip correctness stress tests
// =============================================================================

class RoundtripStressTest : public ::testing::Test {
protected:
    gsst::Codec codec;  // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)

    void roundtrip_check(const std::vector<uint8_t>& data, const gsst::CompressOptions& opts = {}) {
        const size_t bound = gsst::compress_bound(data.size(), opts);
        std::vector<uint8_t> compressed(bound);
        std::vector<uint8_t> decompressed(data.size());

        auto cr = codec.compress(data.data(), data.size(), compressed.data(), compressed.size(), opts);
        ASSERT_EQ(cr.status, gsst::Status::Success) << "Compression failed for data size " << data.size();

        auto dr = codec.decompress(compressed.data(), cr.compressed_size, decompressed.data(), decompressed.size());
        ASSERT_EQ(dr.status, gsst::Status::Success) << "Decompression failed for data size " << data.size();
        ASSERT_EQ(dr.decompressed_size, data.size());
        EXPECT_EQ(data, decompressed) << "Data mismatch after roundtrip";
    }
};

TEST_F(RoundtripStressTest, VariousSizes) {
    // Test a range of sizes from tiny to medium
    for (const size_t size : {1,   2,   3,   7,   15,  16,   17,   31,   32,   33,    63,    64,    65,
                              127, 128, 255, 256, 512, 1023, 1024, 4096, 8192, 16384, 32768, 65536, 131072}) {
        auto data = make_repetitive("test data pattern. ", size);
        SCOPED_TRACE("size=" + std::to_string(size));
        roundtrip_check(data);
    }
}

TEST_F(RoundtripStressTest, RandomDataVariousSeeds) {
    for (uint32_t seed = 0; seed < 10; ++seed) {
        auto data = make_random(50000, 256, seed);
        SCOPED_TRACE("seed=" + std::to_string(seed));
        roundtrip_check(data);
    }
}

TEST_F(RoundtripStressTest, LowAlphabetVariousAlphabets) {
    for (const int alpha : {2, 3, 4, 8, 16, 32, 64, 128}) {
        auto data = make_random(50000, alpha);
        SCOPED_TRACE("alphabet=" + std::to_string(alpha));
        roundtrip_check(data);
    }
}

TEST_F(RoundtripStressTest, AllByteValues) {
    // Ensure every possible byte value survives roundtrip
    std::vector<uint8_t> data(static_cast<size_t>(256) * 100);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i % 256);
    }
    roundtrip_check(data);
}

TEST_F(RoundtripStressTest, ByteFF_Heavy) {
    // Data dominated by 0xFF (the escape code) — worst case for FSST
    std::vector<uint8_t> data(50000);
    std::mt19937 rng(99);  // NOLINT(cert-msc32-c,cert-msc51-cpp)
    std::ranges::generate(data,
                          [&]() -> uint8_t { return (rng() % 4 == 0) ? static_cast<uint8_t>(rng() % 256) : 0xFF; });
    roundtrip_check(data);
}

TEST_F(RoundtripStressTest, ByteFE_Heavy) {
    // Data dominated by 0xFE (the pad code)
    const std::vector<uint8_t> data(50000, 0xFE);
    roundtrip_check(data);
}

TEST_F(RoundtripStressTest, LargeInput) {
    // 4 MiB of text data
    auto data = make_repetitive("Large input test. The FSST algorithm handles multi-megabyte inputs. ",
                                static_cast<size_t>(4) * 1024 * 1024);
    roundtrip_check(data);
}

TEST_F(RoundtripStressTest, AllLayoutsPreserveData) {
    auto data = make_repetitive("Layout roundtrip test with all three modes. ", 100000);

    for (auto layout : {gsst::Layout::Blocks, gsst::Layout::Splits, gsst::Layout::Coalesce}) {
        gsst::CompressOptions opts;
        opts.layout = layout;
        opts.num_blocks = 4;
        if (layout != gsst::Layout::Blocks) {
            opts.splits_per_block = 8;
        }
        SCOPED_TRACE("layout=" + std::to_string(static_cast<int>(layout)));
        roundtrip_check(data, opts);
    }
}

TEST_F(RoundtripStressTest, CsvLikeData) {
    // Simulate CSV data with repeated column headers and typed values
    std::string csv;
    csv.reserve(200000);
    csv += "id,name,value,timestamp,status\n";
    std::mt19937 rng(42);  // NOLINT(cert-msc32-c,cert-msc51-cpp)
    for (int row = 0; row < 3000; ++row) {
        csv += std::to_string(row) + ",";
        csv += "item_" + std::to_string(rng() % 100) + ",";
        csv += std::to_string(rng() % 10000) + ".";
        csv += std::to_string(rng() % 100) + ",";
        csv += "2024-01-" + std::to_string(1 + (rng() % 28)) + ",";
        csv += (rng() % 2 == 0 ? "active" : "inactive");
        csv += "\n";
    }
    const std::vector<uint8_t> data(csv.begin(), csv.end());
    roundtrip_check(data);

    // CSV should compress reasonably well
    const double ratio = compress_and_ratio(codec, data);
    EXPECT_GT(ratio, 1.5) << "CSV data should compress > 1.5x";
}

TEST_F(RoundtripStressTest, JsonLikeData) {
    // Simulate JSON data with repeated keys
    std::string json;
    json.reserve(200000);
    json += "[";
    std::mt19937 rng(77);  // NOLINT(cert-msc32-c,cert-msc51-cpp)
    for (int i = 0; i < 2000; ++i) {
        if (i > 0) {
            json += ",";
        }
        json += R"({"id":)" + std::to_string(i) + R"(,"name":"user_)" + std::to_string(rng() % 100) + R"(","score":)" +
                std::to_string(rng() % 1000) + R"(,"active":)" + ((rng() % 2 != 0U) ? "true" : "false") + "}";
    }
    json += "]";
    const std::vector<uint8_t> data(json.begin(), json.end());
    roundtrip_check(data);

    const double ratio = compress_and_ratio(codec, data);
    EXPECT_GT(ratio, 1.5) << "JSON data should compress > 1.5x";
}

// =============================================================================
// Encoder/decoder consistency tests
// =============================================================================

TEST(EncoderDecoderConsistency, EncoderProducesDecodableOutput) {
    // Manually encode using the EncoderTable, then decode using the DecoderTable
    // to verify they agree
    auto data = make_repetitive("consistency check pattern ", 10000);

    auto* encoder = build_encoder_table(data.data(), data.size());
    ASSERT_NE(encoder, nullptr);

    // Encode manually
    std::vector<uint8_t> encoded;
    encoded.reserve(data.size() * 2);
    size_t pos = 0;
    while (pos < data.size()) {
        const int remaining = static_cast<int>(data.size() - pos);
        const uint16_t match = encoder->findLongest(data.data() + pos, remaining);
        const auto code = static_cast<uint8_t>(match);
        const auto len = static_cast<uint8_t>(match >> 8);

        if (code == ESC_CODE) {
            encoded.push_back(ESC_CODE);
            encoded.push_back(data[pos]);
            pos += 1;
        } else {
            encoded.push_back(code);
            pos += len;
        }
    }

    // Decode manually using the decoder table
    const auto& dec = encoder->decoder;
    std::vector<uint8_t> decoded;
    decoded.reserve(data.size());
    size_t enc_pos = 0;
    while (enc_pos < encoded.size()) {
        const uint8_t code = encoded[enc_pos++];
        if (code == ESC_CODE) {
            ASSERT_LT(enc_pos, encoded.size());
            decoded.push_back(encoded[enc_pos++]);
        } else {
            const uint8_t sym_len = dec.len[code];
            ASSERT_GT(sym_len, 0) << "Code " << static_cast<int>(code) << " has zero length in decoder table";
            const auto* sym_bytes = reinterpret_cast<const uint8_t*>(&dec.symbol[code]);
            decoded.insert(decoded.end(), sym_bytes, sym_bytes + sym_len);
        }
    }

    ASSERT_EQ(decoded.size(), data.size());
    EXPECT_EQ(data, decoded) << "Manual encode/decode disagrees";

    free_encoder_table(encoder);
}
