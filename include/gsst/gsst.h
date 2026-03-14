/* ==========================================================================
 * GSST — GPU Static Symbol Table Compression
 * ==========================================================================
 * Plain C API for integration with non-C++ projects and FFI bindings.
 * ========================================================================== */

#ifndef GSST_H
#define GSST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Status codes -------------------------------------------------------- */

typedef enum {
  GSST_SUCCESS = 0,
  GSST_ERROR_INVALID_ARGUMENT = 1,
  GSST_ERROR_BUFFER_TOO_SMALL = 2,
  GSST_ERROR_BAD_ALIGNMENT = 3,
  GSST_ERROR_CUDA_ERROR = 4,
  GSST_ERROR_CORRUPT_DATA = 5,
  GSST_ERROR_CORRUPT_HEADER = 6,
  GSST_ERROR_UNSUPPORTED = 7,
  GSST_ERROR_INTERNAL = 8,
} gsst_status;

/* ---- Layout selection ---------------------------------------------------- */

typedef enum {
  GSST_LAYOUT_BLOCKS = 0,
  GSST_LAYOUT_SPLITS = 1,
  GSST_LAYOUT_COALESCE = 2,
} gsst_layout;

/* ---- Opaque codec handle ------------------------------------------------- */

typedef struct gsst_codec gsst_codec;

gsst_codec *gsst_codec_create(void);
void gsst_codec_destroy(gsst_codec *codec);

/* ---- Buffer size queries ------------------------------------------------- */

/** Upper bound on compressed output size. */
size_t gsst_compress_bound(size_t input_size, gsst_layout layout);

/** Read decompressed size from a compressed buffer header. Returns 0 on error.
 */
size_t gsst_get_decompressed_size(const uint8_t *compressed,
                                  size_t compressed_size);

/* ---- Host-memory compression/decompression ------------------------------- */

gsst_status gsst_compress(const gsst_codec *codec, const uint8_t *src,
                          size_t src_size, uint8_t *dst, size_t dst_capacity,
                          size_t *compressed_size_out, gsst_layout layout,
                          uint32_t num_blocks, uint32_t splits_per_block);

gsst_status gsst_decompress(const gsst_codec *codec, const uint8_t *src,
                            size_t src_size, uint8_t *dst, size_t dst_capacity,
                            size_t *decompressed_size_out);

/* ---- Version ------------------------------------------------------------- */

const char *gsst_version_string(void);
const char *gsst_status_string(gsst_status status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GSST_H */
