# =============================================================================
# GSST — Benchmarks
# =============================================================================

add_executable(gsst_compress_bench compress_bench.cpp)
target_link_libraries(gsst_compress_bench PRIVATE gsst::gsst benchmark::benchmark benchmark::benchmark_main)

add_executable(gsst_decompress_bench decompress_bench.cpp)
target_link_libraries(gsst_decompress_bench PRIVATE gsst::gsst benchmark::benchmark benchmark::benchmark_main)
