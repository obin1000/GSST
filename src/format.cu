// =============================================================================
// GSST — Compressed data format read/write
// =============================================================================

#include <cstring>
#include <gsst/detail/format.hpp>


namespace gsst::detail {

size_t write_file_header(uint8_t *dst, const FileHeader &header) {
  std::memcpy(dst, &header, sizeof(FileHeader));
  return sizeof(FileHeader);
}

bool read_file_header(const uint8_t *src, size_t src_size, FileHeader &header) {
  if (src_size < sizeof(FileHeader))
    return false;

  std::memcpy(&header, src, sizeof(FileHeader));
  return header.is_valid();
}

} // namespace gsst::detail
