// =============================================================================
// GSST — Decompression benchmarks
// =============================================================================
// Measures decompression throughput (GB/s) and compression ratio for various
// data patterns and sizes.
// =============================================================================
#include <algorithm>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <gsst/gsst.hpp>
#include <random>
#include <string>
#include <utility>
#include <vector>


// ---------------------------------------------------------------------------
// Data generators (same as compress_bench)
// ---------------------------------------------------------------------------
static std::vector<uint8_t> gen_repetitive(size_t size) {
    const std::string pattern = "The quick brown fox jumps over the lazy dog. ";
    std::vector<uint8_t> data;
    data.reserve(size);
    while (data.size() < size) {
        const size_t chunk = std::min(pattern.size(), size - data.size());
        data.insert(data.end(), pattern.begin(), pattern.begin() + static_cast<std::ptrdiff_t>(chunk));
    }
    return data;
}

static std::vector<uint8_t> gen_random(size_t size, int alphabet = 256) {
    std::mt19937 rng(123);  // NOLINT(cert-msc32-c,cert-msc51-cpp)
    std::uniform_int_distribution<int> dist(0, alphabet - 1);
    std::vector<uint8_t> data(size);
    std::ranges::generate(data, [&]() { return static_cast<uint8_t>(dist(rng)); });
    return data;
}

static std::vector<uint8_t> gen_csv(size_t target_size) {
    std::string csv;
    csv.reserve(target_size + 1024);
    csv += "id,name,value,timestamp,status\n";
    std::mt19937 rng(42);  // NOLINT(cert-msc32-c,cert-msc51-cpp)
    for (int row = 0; csv.size() < target_size; ++row) {
        csv += std::to_string(row) + ",";
        csv += "item_" + std::to_string(rng() % 100) + ",";
        csv += std::to_string(rng() % 10000) + "." + std::to_string(rng() % 100) + ",";
        csv += "2024-01-" + std::to_string(1 + (rng() % 28)) + ",";
        csv += (rng() % 2 == 0 ? "active" : "inactive");
        csv += "\n";
    }
    csv.resize(target_size);
    return {csv.begin(), csv.end()};
}

// ---------------------------------------------------------------------------
// Pre-compressed dataset holder
// ---------------------------------------------------------------------------
struct CompressedData {
    std::vector<uint8_t> original;
    std::vector<uint8_t> compressed;
    size_t compressed_size = 0;
};

static CompressedData prepare_compressed(std::vector<uint8_t> data, gsst::Layout layout = gsst::Layout::Splits) {
    CompressedData cd;
    cd.original = std::move(data);

    const gsst::Codec codec;
    gsst::CompressOptions opts;
    opts.layout = layout;

    const size_t bound = gsst::compress_bound(cd.original.size(), opts);
    cd.compressed.resize(bound);

    auto cr = codec.compress(cd.original.data(), cd.original.size(), cd.compressed.data(), cd.compressed.size(), opts);
    cd.compressed_size = cr.compressed_size;
    cd.compressed.resize(cd.compressed_size);
    return cd;
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_DecompressRepetitive(benchmark::State& state) {
    const auto size = static_cast<size_t>(state.range(0));
    auto cd = prepare_compressed(gen_repetitive(size));
    const gsst::Codec codec;
    std::vector<uint8_t> output(cd.original.size());

    for (auto _ : state) {
        auto result = codec.decompress(cd.compressed.data(), cd.compressed_size, output.data(), output.size());
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(cd.original.size()));
    state.counters["ratio"] =
        benchmark::Counter(static_cast<double>(cd.original.size()) / static_cast<double>(cd.compressed_size));
    state.counters["throughput_GBps"] = benchmark::Counter(static_cast<double>(cd.original.size()) / 1e9,
                                                           benchmark::Counter::kIsIterationInvariantRate);
    state.SetLabel("repetitive");
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_DecompressRandom(benchmark::State& state) {
    const auto size = static_cast<size_t>(state.range(0));
    auto cd = prepare_compressed(gen_random(size));
    const gsst::Codec codec;
    std::vector<uint8_t> output(cd.original.size());

    for (auto _ : state) {
        auto result = codec.decompress(cd.compressed.data(), cd.compressed_size, output.data(), output.size());
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(cd.original.size()));
    state.counters["ratio"] =
        benchmark::Counter(static_cast<double>(cd.original.size()) / static_cast<double>(cd.compressed_size));
    state.counters["throughput_GBps"] = benchmark::Counter(static_cast<double>(cd.original.size()) / 1e9,
                                                           benchmark::Counter::kIsIterationInvariantRate);
    state.SetLabel("random");
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_DecompressCsv(benchmark::State& state) {
    const auto size = static_cast<size_t>(state.range(0));
    auto cd = prepare_compressed(gen_csv(size));
    const gsst::Codec codec;
    std::vector<uint8_t> output(cd.original.size());

    for (auto _ : state) {
        auto result = codec.decompress(cd.compressed.data(), cd.compressed_size, output.data(), output.size());
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(cd.original.size()));
    state.counters["ratio"] =
        benchmark::Counter(static_cast<double>(cd.original.size()) / static_cast<double>(cd.compressed_size));
    state.counters["throughput_GBps"] = benchmark::Counter(static_cast<double>(cd.original.size()) / 1e9,
                                                           benchmark::Counter::kIsIterationInvariantRate);
    state.SetLabel("csv");
}

// Benchmark different layouts
// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_DecompressLayouts(benchmark::State& state) {
    const size_t size = 1 << 20;  // 1 MiB
    auto layout = static_cast<gsst::Layout>(state.range(0));
    auto cd = prepare_compressed(gen_repetitive(size), layout);
    const gsst::Codec codec;
    std::vector<uint8_t> output(cd.original.size());

    for (auto _ : state) {
        auto result = codec.decompress(cd.compressed.data(), cd.compressed_size, output.data(), output.size());
        benchmark::DoNotOptimize(result);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(cd.original.size()));
    state.counters["ratio"] =
        benchmark::Counter(static_cast<double>(cd.original.size()) / static_cast<double>(cd.compressed_size));
    state.counters["throughput_GBps"] = benchmark::Counter(static_cast<double>(cd.original.size()) / 1e9,
                                                           benchmark::Counter::kIsIterationInvariantRate);
}

// Register benchmarks
BENCHMARK(BM_DecompressRepetitive)
    ->RangeMultiplier(4)
    ->Range(1 << 12, 1 << 24)  // 4K to 16M
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_DecompressRandom)->RangeMultiplier(4)->Range(1 << 12, 1 << 24)->Unit(benchmark::kMillisecond);

BENCHMARK(BM_DecompressCsv)->RangeMultiplier(4)->Range(1 << 12, 1 << 24)->Unit(benchmark::kMillisecond);

BENCHMARK(BM_DecompressLayouts)
    ->Arg(0)  // Blocks
    ->Arg(1)  // Splits
    ->Arg(2)  // Coalesce
    ->Unit(benchmark::kMillisecond);
