// =============================================================================
// GSST — C API tests
// =============================================================================
#include <cstdint>
#include <cstring>
#include <gsst/gsst.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>


TEST(CApiTest, CreateAndDestroy) {
  gsst_codec *codec = gsst_codec_create();
  ASSERT_NE(codec, nullptr);
  gsst_codec_destroy(codec);
}

TEST(CApiTest, VersionString) {
  const char *ver = gsst_version_string();
  ASSERT_NE(ver, nullptr);
  EXPECT_GT(strlen(ver), 0u);
}

TEST(CApiTest, StatusStrings) {
  EXPECT_STREQ(gsst_status_string(GSST_SUCCESS), "Success");
  EXPECT_STREQ(gsst_status_string(GSST_ERROR_INVALID_ARGUMENT),
               "Invalid argument");
  EXPECT_STREQ(gsst_status_string(GSST_ERROR_CUDA_ERROR), "CUDA error");
}

TEST(CApiTest, CompressDecompressRoundtrip) {
  gsst_codec *codec = gsst_codec_create();
  ASSERT_NE(codec, nullptr);

  // Create test data
  std::string input;
  for (int i = 0; i < 500; ++i) {
    input += "Hello GSST C API! ";
  }

  const size_t bound = gsst_compress_bound(input.size(), GSST_LAYOUT_BLOCKS);
  std::vector<uint8_t> compressed(bound);
  size_t compressed_size = 0;

  gsst_status status =
      gsst_compress(codec, reinterpret_cast<const uint8_t *>(input.data()),
                    input.size(), compressed.data(), compressed.size(),
                    &compressed_size, GSST_LAYOUT_BLOCKS, 0, 0);
  ASSERT_EQ(status, GSST_SUCCESS);
  ASSERT_GT(compressed_size, 0u);

  // Query decompressed size
  const size_t decomp_size =
      gsst_get_decompressed_size(compressed.data(), compressed_size);
  ASSERT_EQ(decomp_size, input.size());

  // Decompress
  std::vector<uint8_t> decompressed(decomp_size);
  size_t actual_decomp = 0;

  status =
      gsst_decompress(codec, compressed.data(), compressed_size,
                      decompressed.data(), decompressed.size(), &actual_decomp);
  ASSERT_EQ(status, GSST_SUCCESS);
  ASSERT_EQ(actual_decomp, input.size());

  EXPECT_EQ(memcmp(decompressed.data(), input.data(), input.size()), 0);

  gsst_codec_destroy(codec);
}

TEST(CApiTest, InvalidArguments) {
  gsst_codec *codec = gsst_codec_create();

  size_t out = 0;
  gsst_status status = gsst_compress(codec, nullptr, 0, nullptr, 0, &out,
                                     GSST_LAYOUT_BLOCKS, 0, 0);
  EXPECT_EQ(status, GSST_ERROR_INVALID_ARGUMENT);

  status = gsst_decompress(codec, nullptr, 0, nullptr, 0, &out);
  EXPECT_EQ(status, GSST_ERROR_INVALID_ARGUMENT);

  gsst_codec_destroy(codec);
}
