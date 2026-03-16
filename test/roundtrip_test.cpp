// =============================================================================
// GSST — Roundtrip (compress → decompress) tests
// =============================================================================
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gsst/gsst.hpp>
#include <gtest/gtest.h>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>


class RoundtripTest : public ::testing::Test {
protected:
  gsst::Codec codec;
};

// ---------------------------------------------------------------------------
// Helper: generate test data
// ---------------------------------------------------------------------------
static std::vector<uint8_t> make_repetitive(const std::string &pattern,
                                            size_t target_size) {
  std::vector<uint8_t> data;
  data.reserve(target_size);
  while (data.size() < target_size) {
    const size_t chunk = std::min(pattern.size(), target_size - data.size());
    data.insert(data.end(), pattern.begin(), pattern.begin() + static_cast<std::ptrdiff_t>(chunk));
  }
  return data;
}

static std::vector<uint8_t> make_random(size_t size, int alphabet_size = 256) {
  std::mt19937 rng(42);  // NOLINT(cert-msc32-c,cert-msc51-cpp)
  std::uniform_int_distribution<int> dist(0, alphabet_size - 1);
  std::vector<uint8_t> data(size);
  std::ranges::generate(data, [&]() { return static_cast<uint8_t>(dist(rng)); });
  return data;
}

static std::vector<uint8_t> make_sequential(size_t size) {
  std::vector<uint8_t> data(size);
  std::iota(data.begin(), data.end(), 0);
  return data;
}

// ---------------------------------------------------------------------------
// Roundtrip tests using the convenience string API
// ---------------------------------------------------------------------------

TEST_F(RoundtripTest, SmallString) {
  const std::string input = "Hello, GSST! This is a test string.";
  auto compressed = codec.compress(input);
  ASSERT_FALSE(compressed.empty()) << "Compression failed";

  auto decompressed =
      codec.decompress_string(compressed.data(), compressed.size());
  EXPECT_EQ(decompressed, input);
}

TEST_F(RoundtripTest, RepetitiveData) {
  auto data =
      make_repetitive("The quick brown fox jumps over the lazy dog. ", 100000);
  const std::string_view sv(reinterpret_cast<const char *>(data.data()), data.size());

  auto compressed = codec.compress(sv);
  ASSERT_FALSE(compressed.empty());

  // Repetitive data should compress well
  EXPECT_LT(compressed.size(), data.size());

  auto decompressed =
      codec.decompress_string(compressed.data(), compressed.size());
  EXPECT_EQ(decompressed.size(), data.size());
  EXPECT_EQ(std::memcmp(decompressed.data(), data.data(), data.size()), 0);
}

TEST_F(RoundtripTest, RandomDataSmallAlphabet) {
  auto data = make_random(50000, 10); // Only 10 distinct byte values
  const std::string_view sv(reinterpret_cast<const char *>(data.data()), data.size());

  auto compressed = codec.compress(sv);
  ASSERT_FALSE(compressed.empty());

  auto decompressed =
      codec.decompress_string(compressed.data(), compressed.size());
  ASSERT_EQ(decompressed.size(), data.size());
  EXPECT_EQ(std::memcmp(decompressed.data(), data.data(), data.size()), 0);
}

TEST_F(RoundtripTest, RandomDataFullAlphabet) {
  auto data = make_random(50000, 256);
  const std::string_view sv(reinterpret_cast<const char *>(data.data()), data.size());

  auto compressed = codec.compress(sv);
  ASSERT_FALSE(compressed.empty());

  auto decompressed =
      codec.decompress_string(compressed.data(), compressed.size());
  ASSERT_EQ(decompressed.size(), data.size());
  EXPECT_EQ(std::memcmp(decompressed.data(), data.data(), data.size()), 0);
}

// ---------------------------------------------------------------------------
// Roundtrip tests using the buffer API
// ---------------------------------------------------------------------------

TEST_F(RoundtripTest, BufferAPI) {
  auto data = make_repetitive("ABCDEFGH", 80000);
  const size_t bound = gsst::compress_bound(data.size());
  std::vector<uint8_t> compressed(bound);
  std::vector<uint8_t> decompressed(data.size());

  auto cr = codec.compress(data.data(), data.size(), compressed.data(),
                           compressed.size());
  ASSERT_EQ(cr.status, gsst::Status::Success);
  ASSERT_GT(cr.compressed_size, 0u);

  auto dr = codec.decompress(compressed.data(), cr.compressed_size,
                             decompressed.data(), decompressed.size());
  ASSERT_EQ(dr.status, gsst::Status::Success);
  ASSERT_EQ(dr.decompressed_size, data.size());

  EXPECT_EQ(data, decompressed);
}

// ---------------------------------------------------------------------------
// Layout-specific roundtrips
// ---------------------------------------------------------------------------

TEST_F(RoundtripTest, BlocksLayout) {
  auto data = make_repetitive("blocks layout test data. ", 50000);
  gsst::CompressOptions opts;
  opts.layout = gsst::Layout::Blocks;
  opts.num_blocks = 4;

  const std::string_view sv(reinterpret_cast<const char *>(data.data()), data.size());
  auto compressed = codec.compress(sv, opts);
  ASSERT_FALSE(compressed.empty());

  auto decompressed =
      codec.decompress_string(compressed.data(), compressed.size());
  ASSERT_EQ(decompressed.size(), data.size());
  EXPECT_EQ(std::memcmp(decompressed.data(), data.data(), data.size()), 0);
}

TEST_F(RoundtripTest, SplitsLayout) {
  auto data = make_repetitive("splits layout parallel decompression. ", 50000);
  gsst::CompressOptions opts;
  opts.layout = gsst::Layout::Splits;
  opts.num_blocks = 4;
  opts.splits_per_block = 8;

  const std::string_view sv(reinterpret_cast<const char *>(data.data()), data.size());
  auto compressed = codec.compress(sv, opts);
  ASSERT_FALSE(compressed.empty());

  auto decompressed =
      codec.decompress_string(compressed.data(), compressed.size());
  ASSERT_EQ(decompressed.size(), data.size());
  EXPECT_EQ(std::memcmp(decompressed.data(), data.data(), data.size()), 0);
}

TEST_F(RoundtripTest, CoalesceLayout) {
  auto data = make_repetitive("coalesced memory access pattern. ", 50000);
  gsst::CompressOptions opts;
  opts.layout = gsst::Layout::Coalesce;
  opts.num_blocks = 4;
  opts.splits_per_block = 16;

  const std::string_view sv(reinterpret_cast<const char *>(data.data()), data.size());
  auto compressed = codec.compress(sv, opts);
  ASSERT_FALSE(compressed.empty());

  auto decompressed =
      codec.decompress_string(compressed.data(), compressed.size());
  ASSERT_EQ(decompressed.size(), data.size());
  EXPECT_EQ(std::memcmp(decompressed.data(), data.data(), data.size()), 0);
}

// ---------------------------------------------------------------------------
// get_decompressed_size
// ---------------------------------------------------------------------------

TEST_F(RoundtripTest, GetDecompressedSize) {
  const std::string input = "Size query test data repeated several times. ";
  auto data = make_repetitive(input, 10000);
  const std::string_view sv(reinterpret_cast<const char *>(data.data()), data.size());

  auto compressed = codec.compress(sv);
  ASSERT_FALSE(compressed.empty());

  const size_t reported_size =
      gsst::get_decompressed_size(compressed.data(), compressed.size());
  EXPECT_EQ(reported_size, data.size());
}
