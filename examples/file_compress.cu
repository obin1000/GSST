// =============================================================================
// GSST — File compression example
// =============================================================================
// Demonstrates compressing and decompressing a file from disk.
// =============================================================================

#include <cstdio>
#include <fstream>
#include <gsst/gsst.hpp>
#include <vector>


static std::vector<uint8_t> read_file(const char *path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    return {};
  auto size = f.tellg();
  f.seekg(0);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  f.read(reinterpret_cast<char *>(data.data()), size);
  return data;
}

static bool write_file(const char *path, const uint8_t *data, size_t size) {
  std::ofstream f(path, std::ios::binary);
  if (!f)
    return false;
  f.write(reinterpret_cast<const char *>(data), size);
  return f.good();
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <input-file> [output-file.gsst]\n", argv[0]);
    return 1;
  }

  const char *input_path = argv[1];
  std::string output_path =
      (argc >= 3) ? argv[2] : std::string(input_path) + ".gsst";

  // Read input file
  auto data = read_file(input_path);
  if (data.empty()) {
    fprintf(stderr, "Failed to read '%s'\n", input_path);
    return 1;
  }
  printf("Read %zu bytes from '%s'\n", data.size(), input_path);

  // Compress
  gsst::Codec codec;
  gsst::CompressOptions opts;
  opts.layout = gsst::Layout::Splits;

  const size_t bound = gsst::compress_bound(data.size(), opts);
  std::vector<uint8_t> compressed(bound);

  auto cr = codec.compress(data.data(), data.size(), compressed.data(),
                           compressed.size(), opts);
  if (cr.status != gsst::Status::Success) {
    fprintf(stderr, "Compression failed: %s\n", gsst::status_string(cr.status));
    return 1;
  }

  printf("Compressed to %zu bytes (%.2fx ratio)\n", cr.compressed_size,
         static_cast<double>(data.size()) / cr.compressed_size);

  // Write compressed file
  if (!write_file(output_path.c_str(), compressed.data(), cr.compressed_size)) {
    fprintf(stderr, "Failed to write '%s'\n", output_path.c_str());
    return 1;
  }
  printf("Written to '%s'\n", output_path.c_str());

  // Verify by decompressing
  auto compressed_readback = read_file(output_path.c_str());
  std::vector<uint8_t> decompressed(data.size());

  auto dr =
      codec.decompress(compressed_readback.data(), compressed_readback.size(),
                       decompressed.data(), decompressed.size());
  if (dr.status != gsst::Status::Success) {
    fprintf(stderr, "Verification decompression failed: %s\n",
            gsst::status_string(dr.status));
    return 1;
  }

  bool match = (dr.decompressed_size == data.size()) &&
               (memcmp(data.data(), decompressed.data(), data.size()) == 0);
  printf("Verification: %s\n", match ? "PASS" : "FAIL");

  return match ? 0 : 1;
}
