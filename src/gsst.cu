// =============================================================================
// GSST — Main Codec implementation
// =============================================================================
#include <cstring>
#include <gsst/detail/common.hpp>
#include <gsst/detail/format.hpp>
#include <gsst/detail/symbol_table.hpp>
#include <gsst/gsst.h>
#include <gsst/gsst.hpp>
#include <vector>


namespace gsst {

// =============================================================================
// Status to string
// =============================================================================
const char* status_string(Status s) noexcept {
    switch (s) {
    case Status::Success: return "Success";
    case Status::ErrorInvalidArgument: return "Invalid argument";
    case Status::ErrorBufferTooSmall: return "Buffer too small";
    case Status::ErrorBadAlignment: return "Bad alignment";
    case Status::ErrorCudaError: return "CUDA error";
    case Status::ErrorCorruptData: return "Corrupt data";
    case Status::ErrorCorruptHeader: return "Corrupt header";
    case Status::ErrorUnsupportedFormat: return "Unsupported format";
    case Status::ErrorInternal: return "Internal error";
    }
    return "Unknown";
}

// =============================================================================
// Buffer size queries
// =============================================================================
size_t compress_bound(size_t input_size, const CompressOptions& /*opts*/) noexcept {
    // Worst case: each byte becomes escape + literal (2× expansion),
    // plus header, tables, and block descriptors.
    // We add generous overhead for headers.
    if (input_size == 0)
        return sizeof(detail::FileHeader);
    constexpr size_t header_overhead = sizeof(detail::FileHeader) + 4096;  // tables + descriptors
    return input_size * 2 + header_overhead;
}

size_t get_decompressed_size(const uint8_t* compressed_data, size_t compressed_size) noexcept {
    detail::FileHeader hdr{};
    if (!detail::read_file_header(compressed_data, compressed_size, hdr))
        return 0;
    return hdr.uncompressed_size;
}

// =============================================================================
// Codec::Impl
// =============================================================================
struct Codec::Impl {
    // No persistent state needed yet — the codec is stateless.
    // This pimpl exists so we can add caching, device context, etc. later
    // without breaking ABI.
};

// =============================================================================
// Codec construction / destruction / move
// =============================================================================
Codec::Codec() : impl_(std::make_unique<Impl>()) {}
Codec::~Codec() = default;
Codec::Codec(Codec&&) noexcept = default;
Codec& Codec::operator=(Codec&&) noexcept = default;

// =============================================================================
// Host-memory compress
// =============================================================================
CompressResult Codec::compress(const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_capacity,
                               const CompressOptions& opts) const {
    CompressResult result{};

    // Validate arguments
    if (!src || !dst || src_size == 0) {
        result.status = Status::ErrorInvalidArgument;
        return result;
    }

    // Determine block count
    uint32_t num_blocks = opts.num_blocks;
    if (num_blocks == 0) {
        num_blocks = static_cast<uint32_t>((src_size + detail::DEFAULT_BLOCK_SIZE - 1) / detail::DEFAULT_BLOCK_SIZE);
        if (num_blocks == 0)
            num_blocks = 1;
    }

    uint32_t splits_per_block = opts.splits_per_block;
    if (splits_per_block == 0) {
        switch (opts.layout) {
        case Layout::Blocks: splits_per_block = 1; break;
        case Layout::Splits: splits_per_block = 32; break;
        case Layout::Coalesce: splits_per_block = 32; break;
        }
    }

    const size_t block_size = detail::compute_block_size(src_size, num_blocks);

    // --- Build symbol table from full input ---------------------------------
    // The FSST algorithm samples internally (16KB), so passing the full input
    // gives it maximum statistical coverage.
    detail::EncoderTable* encoder = detail::build_encoder_table(src, src_size);
    if (!encoder) {
        result.status = Status::ErrorInternal;
        return result;
    }

    // --- Serialize decoder table --------------------------------------------
    uint8_t table_buf[2200];
    const size_t table_serialized_size = encoder->decoder.export_to(table_buf, sizeof(table_buf));

    // --- Write file header --------------------------------------------------
    detail::FileHeader hdr{};
    hdr.magic = detail::FileHeader::MAGIC;
    hdr.version = static_cast<uint32_t>(detail::FORMAT_VERSION);
    hdr.layout = static_cast<uint8_t>(opts.layout);
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));
    hdr.uncompressed_size = src_size;
    hdr.num_tables = 1;
    hdr.num_blocks = num_blocks;
    hdr.table_section_size = static_cast<uint32_t>(table_serialized_size);
    hdr.splits_per_block = splits_per_block;

    size_t write_pos = 0;

    // Reserve space for header
    write_pos += sizeof(detail::FileHeader);

    // Write table section
    if (write_pos + table_serialized_size > dst_capacity) {
        detail::free_encoder_table(encoder);
        result.status = Status::ErrorBufferTooSmall;
        return result;
    }
    std::memcpy(dst + write_pos, table_buf, table_serialized_size);
    write_pos += table_serialized_size;

    // Reserve space for block descriptors
    const size_t descriptors_offset = write_pos;
    write_pos += num_blocks * sizeof(detail::BlockDescriptor);

    // --- Encode each block (fast CPU path with hash-based lookup) -----------
    std::vector<detail::BlockDescriptor> descriptors(num_blocks);

    for (uint32_t b = 0; b < num_blocks; ++b) {
        const size_t block_start = static_cast<size_t>(b) * block_size;
        const size_t this_block_size = std::min(block_size, src_size - block_start);
        const uint8_t* block_src = src + block_start;

        // Worst-case expansion: each byte → ESC + literal
        const size_t max_compressed = this_block_size * 2;
        if (write_pos + max_compressed > dst_capacity) {
            detail::free_encoder_table(encoder);
            result.status = Status::ErrorBufferTooSmall;
            return result;
        }

        uint8_t* block_dst = dst + write_pos;
        size_t out_pos = 0;
        size_t in_pos = 0;
        const int block_len = static_cast<int>(this_block_size);

        while (in_pos < this_block_size) {
            const int remaining = block_len - static_cast<int>(in_pos);
            const uint16_t match = encoder->findLongest(block_src + in_pos, remaining);
            const uint8_t code = static_cast<uint8_t>(match);
            const uint8_t sym_len = static_cast<uint8_t>(match >> 8);

            if (code == detail::ESC_CODE) {
                // Escape: emit ESC + literal byte
                block_dst[out_pos++] = detail::ESC_CODE;
                block_dst[out_pos++] = block_src[in_pos];
                in_pos += 1;
            } else {
                block_dst[out_pos++] = code;
                in_pos += sym_len;
            }
        }

        descriptors[b].compressed_size = static_cast<uint32_t>(out_pos);
        descriptors[b].uncompressed_size = static_cast<uint32_t>(this_block_size);
        descriptors[b].table_index = 0;
        descriptors[b].num_splits = splits_per_block;
        write_pos += out_pos;
    }

    // --- Write block descriptors ----------------------------------------
    std::memcpy(dst + descriptors_offset, descriptors.data(), num_blocks * sizeof(detail::BlockDescriptor));

    // --- Finalize file header -------------------------------------------
    hdr.compressed_size = write_pos;
    detail::write_file_header(dst, hdr);

    result.status = Status::Success;
    result.compressed_size = write_pos;
    result.uncompressed_size = src_size;

    detail::free_encoder_table(encoder);
    return result;
}

// =============================================================================
// Host-memory decompress
// =============================================================================
DecompressResult Codec::decompress(const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_capacity,
                                   const DecompressOptions& /*opts*/) const {
    DecompressResult result{};

    if (!src || !dst || src_size == 0) {
        result.status = Status::ErrorInvalidArgument;
        return result;
    }

    // Read file header
    detail::FileHeader hdr{};
    if (!detail::read_file_header(src, src_size, hdr)) {
        result.status = Status::ErrorCorruptHeader;
        return result;
    }

    if (hdr.uncompressed_size > dst_capacity) {
        result.status = Status::ErrorBufferTooSmall;
        return result;
    }

    // Read decoder table
    detail::DecoderTable decoder{};
    const size_t tbl_off = detail::table_section_offset();
    if (tbl_off + hdr.table_section_size > src_size) {
        result.status = Status::ErrorCorruptData;
        return result;
    }
    if (decoder.import_from(src + tbl_off, hdr.table_section_size) == 0) {
        result.status = Status::ErrorCorruptHeader;
        return result;
    }

    // Read block descriptors
    const size_t bd_off = detail::block_descriptors_offset(hdr);
    const size_t bd_end = bd_off + hdr.num_blocks * sizeof(detail::BlockDescriptor);
    if (bd_end > src_size) {
        result.status = Status::ErrorCorruptData;
        return result;
    }
    auto* descriptors = reinterpret_cast<const detail::BlockDescriptor*>(src + bd_off);

    // Decompress each block (CPU reference)
    // TODO: dispatch to GPU kernels (decode_blocks / decode_splits /
    // decode_coalesce)
    const size_t data_off = detail::data_section_offset(hdr);
    size_t src_pos = data_off;
    size_t dst_pos = 0;

    for (uint32_t b = 0; b < hdr.num_blocks; ++b) {
        const auto& bd = descriptors[b];
        const uint8_t* block_src = src + src_pos;
        uint8_t* block_dst = dst + dst_pos;
        size_t out_pos = 0;
        size_t in_pos = 0;

        while (in_pos < bd.compressed_size) {
            const uint8_t code = block_src[in_pos++];
            if (code == detail::ESC_CODE) {
                if (in_pos >= bd.compressed_size) {
                    result.status = Status::ErrorCorruptData;
                    return result;
                }
                block_dst[out_pos++] = block_src[in_pos++];
            } else {
                const uint8_t sym_len = decoder.len[code];
                const uint8_t* sym_bytes = reinterpret_cast<const uint8_t*>(&decoder.symbol[code]);
                std::memcpy(block_dst + out_pos, sym_bytes, sym_len);
                out_pos += sym_len;
            }
        }

        if (out_pos != bd.uncompressed_size) {
            result.status = Status::ErrorCorruptData;
            return result;
        }

        src_pos += bd.compressed_size;
        dst_pos += out_pos;
    }

    result.status = Status::Success;
    result.decompressed_size = dst_pos;
    return result;
}

// =============================================================================
// Device-memory compress / decompress — stubs for now
// =============================================================================
CompressResult Codec::compress_device(const uint8_t* /*d_src*/, size_t /*src_size*/, uint8_t* /*d_dst*/,
                                      size_t /*dst_capacity*/, const CompressOptions& /*opts*/) const {
    // TODO: Implement full GPU pipeline (sample → build table → encode kernels)
    return CompressResult{.status = Status::ErrorInternal, .compressed_size = 0};
}

DecompressResult Codec::decompress_device(const uint8_t* /*d_src*/, size_t /*src_size*/, uint8_t* /*d_dst*/,
                                          size_t /*dst_capacity*/, const DecompressOptions& /*opts*/) const {
    // TODO: Implement GPU decompression dispatching to layout-specific kernels
    return DecompressResult{.status = Status::ErrorInternal, .decompressed_size = 0};
}

// =============================================================================
// Convenience wrappers
// =============================================================================
std::vector<uint8_t> Codec::compress(std::string_view input, const CompressOptions& opts) const {
    const size_t bound = compress_bound(input.size(), opts);
    std::vector<uint8_t> buf(bound);
    auto res = compress(reinterpret_cast<const uint8_t*>(input.data()), input.size(), buf.data(), buf.size(), opts);
    if (res.status != Status::Success)
        return {};
    buf.resize(res.compressed_size);
    return buf;
}

std::string Codec::decompress_string(const uint8_t* src, size_t src_size, const DecompressOptions& opts) const {
    size_t orig_size = get_decompressed_size(src, src_size);
    if (orig_size == 0)
        return {};
    std::string out(orig_size, '\0');
    auto res = decompress(src, src_size, reinterpret_cast<uint8_t*>(out.data()), out.size(), opts);
    if (res.status != Status::Success)
        return {};
    out.resize(res.decompressed_size);
    return out;
}

}  // namespace gsst

// =============================================================================
// C API implementation
// =============================================================================
extern "C" {

gsst_codec* gsst_codec_create(void) {
    auto* codec = new (std::nothrow) gsst::Codec();
    return reinterpret_cast<gsst_codec*>(codec);
}

void gsst_codec_destroy(gsst_codec* codec) {
    delete reinterpret_cast<gsst::Codec*>(codec);
}

size_t gsst_compress_bound(size_t input_size, gsst_layout layout) {
    gsst::CompressOptions opts;
    opts.layout = static_cast<gsst::Layout>(layout);
    return gsst::compress_bound(input_size, opts);
}

size_t gsst_get_decompressed_size(const uint8_t* compressed, size_t compressed_size) {
    return gsst::get_decompressed_size(compressed, compressed_size);
}

gsst_status gsst_compress(const gsst_codec* codec, const uint8_t* src, size_t src_size, uint8_t* dst,
                          size_t dst_capacity, size_t* compressed_size_out, gsst_layout layout, uint32_t num_blocks,
                          uint32_t splits_per_block) {
    auto* cpp_codec = reinterpret_cast<const gsst::Codec*>(codec);
    gsst::CompressOptions opts;
    opts.layout = static_cast<gsst::Layout>(layout);
    opts.num_blocks = num_blocks;
    opts.splits_per_block = splits_per_block;
    auto res = cpp_codec->compress(src, src_size, dst, dst_capacity, opts);
    if (compressed_size_out)
        *compressed_size_out = res.compressed_size;
    return static_cast<gsst_status>(res.status);
}

gsst_status gsst_decompress(const gsst_codec* codec, const uint8_t* src, size_t src_size, uint8_t* dst,
                            size_t dst_capacity, size_t* decompressed_size_out) {
    auto* cpp_codec = reinterpret_cast<const gsst::Codec*>(codec);
    auto res = cpp_codec->decompress(src, src_size, dst, dst_capacity);
    if (decompressed_size_out)
        *decompressed_size_out = res.decompressed_size;
    return static_cast<gsst_status>(res.status);
}

const char* gsst_version_string(void) {
    return "0.1.0";
}

const char* gsst_status_string(gsst_status status) {
    return gsst::status_string(static_cast<gsst::Status>(status));
}

}  // extern "C"
