# GSST — GPU Static Symbol Table Compression

A high-performance GPU-accelerated string compression library based on [FSST (Fast Static Symbol Table)](https://www.vldb.org/pvldb/vol13/p2649-boncz.pdf), providing both compression and decompression on NVIDIA GPUs using CUDA.

## Features

- **High throughput**: GPU compression achieving 74+ GB/s on modern NVIDIA GPUs
- **Three decompression layouts**: optimized for different GPU access patterns
  - **Blocks** — simple 1-thread-per-block, minimal overhead
  - **Splits** — sub-block parallelism with aligned I/O for higher throughput
  - **Coalesce** — interleaved symbols for coalesced global memory access
- **Clean C++ 20 API** with `gsst::Codec` class
- **Plain C API** for FFI / non-C++ integration
- **Convenience wrappers** for `std::string` and `std::string_view`
- **Host and device memory** interfaces
- **Modern CMake** build system with presets, install targets, and `find_package` support

## Quick Start

```cpp
#include <gsst/gsst.hpp>

// Create a codec
gsst::Codec codec;

// Compress a string
std::string data = "Your string data here...";
auto compressed = codec.compress(data);

// Decompress
auto decompressed = codec.decompress_string(compressed.data(), compressed.size());
assert(decompressed == data);
```

### Buffer API

```cpp
gsst::Codec codec;

// Query output buffer size
size_t bound = gsst::compress_bound(input_size);

// Compress
gsst::CompressResult cr = codec.compress(src, src_size, dst, dst_capacity);
if (cr.status != gsst::Status::Success) { /* handle error */ }

// Decompress
gsst::DecompressResult dr = codec.decompress(compressed, comp_size, output, output_capacity);
```

### Layout selection

```cpp
gsst::CompressOptions opts;
opts.layout = gsst::Layout::Splits;  // or Blocks, Coalesce
opts.num_blocks = 8;
opts.splits_per_block = 32;

auto compressed = codec.compress(data, opts);
```

### C API

```c
#include <gsst/gsst.h>

gsst_codec* codec = gsst_codec_create();

size_t compressed_size;
gsst_compress(codec, src, src_size, dst, dst_capacity,
              &compressed_size, GSST_LAYOUT_SPLITS, 0, 0);

size_t decompressed_size;
gsst_decompress(codec, compressed, compressed_size,
                output, output_capacity, &decompressed_size);

gsst_codec_destroy(codec);
```

## Building

### Requirements

- CMake 3.25+
- CUDA Toolkit 12.0+ (with `nvcc`)
- C++20 capable compiler (GCC 11+, Clang 14+, MSVC 2022+)
- NVIDIA GPU with compute capability 8.0+ (Ampere or newer)

### Build with CMake Presets

```bash
# Configure and build (Release with tests + benchmarks)
cmake --preset default
cmake --build --preset default

# Run tests
ctest --preset default

# Or manually:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Build Options

| Option | Default | Description |
|---|---|---|
| `GSST_BUILD_TESTS` | `ON` | Build unit tests (requires GTest) |
| `GSST_BUILD_BENCHMARKS` | `ON` | Build benchmarks (requires GBench) |
| `GSST_BUILD_EXAMPLES` | `ON` | Build example programs |
| `GSST_INSTALL` | `ON` | Generate install targets |
| `CMAKE_CUDA_ARCHITECTURES` | `80;86;89;90` | Target GPU architectures |

### Integration via CMake

After installing, or as a subdirectory:

```cmake
# Option 1: find_package (after install)
find_package(gsst REQUIRED)
target_link_libraries(my_app PRIVATE gsst::gsst)

# Option 2: add_subdirectory
add_subdirectory(gsst)
target_link_libraries(my_app PRIVATE gsst::gsst)

# Option 3: FetchContent
include(FetchContent)
FetchContent_Declare(gsst
    GIT_REPOSITORY https://github.com/your-org/gsst.git
    GIT_TAG main)
FetchContent_MakeAvailable(gsst)
target_link_libraries(my_app PRIVATE gsst::gsst)
```

## Project Structure

```
gsst/
├── CMakeLists.txt              # Main build file
├── CMakePresets.json            # Build presets
├── cmake/
│   ├── dependencies.cmake       # External deps (GTest, GBench)
│   └── gsst-config.cmake.in    # Package config template
├── include/gsst/
│   ├── gsst.hpp                 # C++ public API
│   ├── gsst.h                   # C public API
│   └── detail/                  # Internal headers
│       ├── common.hpp           # Shared types, CUDA helpers
│       ├── format.hpp           # Compressed data format
│       ├── symbol_table.hpp     # Symbol table types
│       ├── encoder.cuh          # GPU compression interface
│       └── decoder.cuh          # GPU decompression interface
├── src/
│   ├── gsst.cu                  # Codec implementation
│   ├── symbol_table.cu          # Symbol table construction
│   ├── encoder.cu               # GPU compression kernels
│   ├── decoder.cu               # GPU decompression kernels
│   └── format.cu                # Format read/write
├── test/                        # Unit tests (Google Test)
├── bench/                       # Benchmarks (Google Benchmark)
└── examples/                    # Usage examples
```

## Architecture

GSST implements the FSST compression algorithm optimized for GPU execution:

1. **Symbol Table Construction**: Samples the input data (on GPU), then iteratively builds a symbol table of up to 255 variable-length byte sequences (1–8 bytes each) that maximize compression gain.

2. **Compression**: Each input byte sequence is greedily matched against the symbol table, replacing matches with single-byte codes. The escape code (255) signals a literal byte. GPU compression uses shared-memory encoding tables with warp-level lookups and stream compaction.

3. **Decompression**: Each code is expanded back to its original byte sequence using the decoder table. Three GPU kernel strategies are provided:
   - **Blocks**: Simplest — each thread independently decompresses a full block
   - **Splits**: Blocks are subdivided with recorded start offsets for sub-block parallelism
   - **Coalesce**: Symbols are interleaved across threads for coalesced memory access

### Compressed Format

```
[FileHeader (44 bytes)]
[Serialized DecoderTable(s)]
[BlockDescriptor × num_blocks (16 bytes each)]
[Compressed block data...]
```

## Status

This project unifies two prior research implementations:
- **GPU Compression** (fsst-gpu): Full GPU encoding pipeline achieving 74 GB/s
- **GPU Decompression** (gsst-main): Three layout strategies for parallel decoding

Current state:
- [x] Unified project structure and build system
- [x] Public C++ and C APIs
- [x] CPU reference compression and decompression
- [x] Test suite with roundtrip, format, and edge-case tests
- [x] Benchmark suite
- [ ] GPU compression kernels (port from fsst-gpu)
- [ ] GPU decompression kernels — Blocks layout
- [ ] GPU decompression kernels — Splits layout  
- [ ] GPU decompression kernels — Coalesce layout
- [ ] Device-memory API implementation

## License

See [LICENSE](../LICENSE) for details.

## References

- P. Boncz, T. Neumann, O. Erling. [FSST: Fast Random Access String Compression](https://www.vldb.org/pvldb/vol13/p2649-boncz.pdf). PVLDB 13(11), 2020.
- R. Vonk. GPU-Accelerated FSST Decompression — MSc Thesis.
- T. Anema. GPU-Accelerated FSST Compression — MSc Thesis, ADMS 2025.
