// =============================================================================
// GSST — Format header tests
// =============================================================================
#include <cstring>
#include <gsst/detail/common.hpp>
#include <gsst/detail/format.hpp>
#include <gtest/gtest.h>
#include <vector>


using namespace gsst::detail;

TEST(FileHeaderTest, WriteAndReadRoundtrip) {
  FileHeader original{};
  original.magic = FileHeader::MAGIC;
  original.version = static_cast<uint32_t>(FORMAT_VERSION);
  original.layout = 1;
  std::memset(original.reserved, 0, sizeof(original.reserved));
  original.uncompressed_size = 1024 * 1024;
  original.compressed_size = 512 * 1024;
  original.num_tables = 1;
  original.num_blocks = 4;
  original.table_section_size = 2048;
  original.splits_per_block = 32;

  std::vector<uint8_t> buf(sizeof(FileHeader));
  size_t written = write_file_header(buf.data(), original);
  ASSERT_EQ(written, sizeof(FileHeader));
  ASSERT_EQ(written, 48u);

  FileHeader loaded{};
  ASSERT_TRUE(read_file_header(buf.data(), buf.size(), loaded));

  EXPECT_EQ(loaded.magic, original.magic);
  EXPECT_EQ(loaded.version, original.version);
  EXPECT_EQ(loaded.layout, original.layout);
  EXPECT_EQ(loaded.uncompressed_size, original.uncompressed_size);
  EXPECT_EQ(loaded.compressed_size, original.compressed_size);
  EXPECT_EQ(loaded.num_tables, original.num_tables);
  EXPECT_EQ(loaded.num_blocks, original.num_blocks);
  EXPECT_EQ(loaded.table_section_size, original.table_section_size);
  EXPECT_EQ(loaded.splits_per_block, original.splits_per_block);
}

TEST(FileHeaderTest, RejectTruncatedBuffer) {
  FileHeader hdr{};
  std::vector<uint8_t> buf(10, 0); // Too small
  EXPECT_FALSE(read_file_header(buf.data(), buf.size(), hdr));
}

TEST(FileHeaderTest, RejectBadMagic) {
  FileHeader original{};
  original.magic = 0xDEADBEEF; // Wrong magic
  original.version = static_cast<uint32_t>(FORMAT_VERSION);

  std::vector<uint8_t> buf(sizeof(FileHeader));
  write_file_header(buf.data(), original);

  FileHeader loaded{};
  EXPECT_FALSE(read_file_header(buf.data(), buf.size(), loaded));
}

TEST(FileHeaderTest, OffsetCalculations) {
  FileHeader hdr{};
  hdr.table_section_size = 2048;
  hdr.num_blocks = 4;

  EXPECT_EQ(table_section_offset(), sizeof(FileHeader));
  EXPECT_EQ(block_descriptors_offset(hdr), sizeof(FileHeader) + 2048);
  EXPECT_EQ(data_section_offset(hdr),
            sizeof(FileHeader) + 2048 + 4 * sizeof(BlockDescriptor));
}

TEST(SerializationTest, BigEndianRoundtrip) {
  uint8_t buf[8];

  serialize<1>(42, buf);
  EXPECT_EQ(deserialize<1>(buf), 42u);

  serialize<2>(0x1234, buf);
  EXPECT_EQ(deserialize<2>(buf), 0x1234u);

  serialize<3>(0x123456, buf);
  EXPECT_EQ(deserialize<3>(buf), 0x123456u);

  serialize<4>(0x12345678, buf);
  EXPECT_EQ(deserialize<4>(buf), 0x12345678u);
}
