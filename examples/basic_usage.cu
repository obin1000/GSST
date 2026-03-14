// =============================================================================
// GSST — Basic usage example
// =============================================================================
// Demonstrates the simplest way to compress and decompress data with GSST.
// =============================================================================

#include <cstdio>
#include <gsst/gsst.hpp>
#include <string>


int main() {
  // Create a codec instance
  gsst::Codec codec;

  // ---- Compress a string ------------------------------------------------
  std::string original =
      "Hello, GSST! This is a demonstration of GPU-accelerated "
      "string compression using the FSST algorithm. "
      "Repetitive patterns like AAAA BBBB CCCC AAAA BBBB CCCC "
      "compress very well because FSST learns common byte sequences "
      "from the data and replaces them with short codes. ";

  // Repeat it to make it more interesting
  std::string data;
  for (int i = 0; i < 100; ++i)
    data += original;

  printf("Original size:     %zu bytes\n", data.size());

  // Compress using the string convenience API
  auto compressed = codec.compress(data);

  if (compressed.empty()) {
    fprintf(stderr, "Compression failed!\n");
    return 1;
  }

  printf("Compressed size:   %zu bytes\n", compressed.size());
  printf("Compression ratio: %.2fx\n",
         static_cast<double>(data.size()) / compressed.size());

  // ---- Decompress -------------------------------------------------------
  auto decompressed =
      codec.decompress_string(compressed.data(), compressed.size());

  if (decompressed.empty()) {
    fprintf(stderr, "Decompression failed!\n");
    return 1;
  }

  printf("Decompressed size: %zu bytes\n", decompressed.size());
  printf("Roundtrip OK:      %s\n",
         (decompressed == data) ? "yes" : "NO — MISMATCH!");

  // ---- Query without decompressing --------------------------------------
  size_t reported =
      gsst::get_decompressed_size(compressed.data(), compressed.size());
  printf("Reported original: %zu bytes\n", reported);

  // ---- Using buffer API with options ------------------------------------
  gsst::CompressOptions opts;
  opts.layout = gsst::Layout::Splits;
  opts.num_blocks = 4;
  opts.splits_per_block = 8;

  auto compressed2 = codec.compress(data, opts);
  printf("\nSplits layout:     %zu bytes (%.2fx)\n", compressed2.size(),
         static_cast<double>(data.size()) / compressed2.size());

  return 0;
}
