// =============================================================================
// GSST — Codec edge-case and stress tests
// =============================================================================
#include <cstdint>
#include <cstring>
#include <gsst/gsst.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>


class CodecEdgeCaseTest : public ::testing::Test {
protected:
  gsst::Codec codec;
};

TEST_F(CodecEdgeCaseTest, EmptyInput) {
  std::vector<uint8_t> dst(1024);
  auto cr = codec.compress(nullptr, 0, dst.data(), dst.size());
  EXPECT_NE(cr.status, gsst::Status::Success);
}

TEST_F(CodecEdgeCaseTest, SingleByte) {
  const uint8_t src = 0x42;
  const size_t bound = gsst::compress_bound(1);
  std::vector<uint8_t> compressed(bound);
  std::vector<uint8_t> decompressed(1);

  auto cr = codec.compress(&src, 1, compressed.data(), compressed.size());
  ASSERT_EQ(cr.status, gsst::Status::Success);

  auto dr = codec.decompress(compressed.data(), cr.compressed_size,
                             decompressed.data(), decompressed.size());
  ASSERT_EQ(dr.status, gsst::Status::Success);
  EXPECT_EQ(decompressed[0], 0x42);
}

TEST_F(CodecEdgeCaseTest, AllSameBytes) {
  std::vector<uint8_t> data(10000, 'A');
  const size_t bound = gsst::compress_bound(data.size());
  std::vector<uint8_t> compressed(bound);
  std::vector<uint8_t> decompressed(data.size());

  auto cr = codec.compress(data.data(), data.size(), compressed.data(),
                           compressed.size());
  ASSERT_EQ(cr.status, gsst::Status::Success);

  // Uniform data should compress very well
  EXPECT_LT(cr.compressed_size, data.size());

  auto dr = codec.decompress(compressed.data(), cr.compressed_size,
                             decompressed.data(), decompressed.size());
  ASSERT_EQ(dr.status, gsst::Status::Success);
  EXPECT_EQ(data, decompressed);
}

TEST_F(CodecEdgeCaseTest, AllEscapeBytes) {
  // Data consisting entirely of byte 255 (the escape code)
  // This is the worst case — every byte must be escaped
  std::vector<uint8_t> data(1000, 0xFF);
  const size_t bound = gsst::compress_bound(data.size());
  std::vector<uint8_t> compressed(bound);
  std::vector<uint8_t> decompressed(data.size());

  auto cr = codec.compress(data.data(), data.size(), compressed.data(),
                           compressed.size());
  ASSERT_EQ(cr.status, gsst::Status::Success);

  auto dr = codec.decompress(compressed.data(), cr.compressed_size,
                             decompressed.data(), decompressed.size());
  ASSERT_EQ(dr.status, gsst::Status::Success);
  EXPECT_EQ(data, decompressed);
}

TEST_F(CodecEdgeCaseTest, BinaryData) {
  // All 256 byte values
  std::vector<uint8_t> data;
  for (int repeat = 0; repeat < 100; ++repeat) {
    for (int b = 0; b < 256; ++b) {
      data.push_back(static_cast<uint8_t>(b));
    }
  }

  const size_t bound = gsst::compress_bound(data.size());
  std::vector<uint8_t> compressed(bound);
  std::vector<uint8_t> decompressed(data.size());

  auto cr = codec.compress(data.data(), data.size(), compressed.data(),
                           compressed.size());
  ASSERT_EQ(cr.status, gsst::Status::Success);

  auto dr = codec.decompress(compressed.data(), cr.compressed_size,
                             decompressed.data(), decompressed.size());
  ASSERT_EQ(dr.status, gsst::Status::Success);
  EXPECT_EQ(data, decompressed);
}

TEST_F(CodecEdgeCaseTest, CorruptHeaderRejected) {
  std::vector<uint8_t> garbage(100, 0xAA);
  std::vector<uint8_t> dst(1000);

  auto dr =
      codec.decompress(garbage.data(), garbage.size(), dst.data(), dst.size());
  EXPECT_EQ(dr.status, gsst::Status::ErrorCorruptHeader);
}

TEST_F(CodecEdgeCaseTest, MultipleBlockCompression) {
  std::string pattern =
      "Multi-block compression test with various data patterns. ";
  std::vector<uint8_t> data;
  for (int i = 0; i < 2000; ++i) {
    data.insert(data.end(), pattern.begin(), pattern.end());
  }

  gsst::CompressOptions opts;
  opts.layout = gsst::Layout::Blocks;
  opts.num_blocks = 8;

  const size_t bound = gsst::compress_bound(data.size(), opts);
  std::vector<uint8_t> compressed(bound);
  std::vector<uint8_t> decompressed(data.size());

  auto cr = codec.compress(data.data(), data.size(), compressed.data(),
                           compressed.size(), opts);
  ASSERT_EQ(cr.status, gsst::Status::Success);

  auto dr = codec.decompress(compressed.data(), cr.compressed_size,
                             decompressed.data(), decompressed.size());
  ASSERT_EQ(dr.status, gsst::Status::Success);
  EXPECT_EQ(data, decompressed);
}

TEST_F(CodecEdgeCaseTest, StringConvenienceApi) {
  const std::string input = "Convenience API round-trip test!";
  auto compressed = codec.compress(input);
  ASSERT_FALSE(compressed.empty());

  auto result = codec.decompress_string(compressed.data(), compressed.size());
  EXPECT_EQ(result, input);
}
