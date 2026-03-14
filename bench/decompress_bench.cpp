// =============================================================================
// GSST — Decompression benchmarks
// =============================================================================
#include <benchmark/benchmark.h>
#include <gsst/gsst.hpp>

#include <random>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Data generators (same as compress_bench)
// ---------------------------------------------------------------------------
static std::vector<uint8_t> gen_repetitive(size_t size) {
  const std::string pattern = "The quick brown fox jumps over the lazy dog. ";
  std::vector<uint8_t> data;
  data.reserve(size);
  while (data.size() < size) {
    size_t chunk = std::min(pattern.size(), size - data.size());
    data.insert(data.end(), pattern.begin(), pattern.begin() + chunk);
  }
  return data;
}

static std::vector<uint8_t> gen_random(size_t size, int alphabet = 256) {
  std::mt19937 rng(123);
  std::uniform_int_distribution<int> dist(0, alphabet - 1);
  std::vector<uint8_t> data(size);
  for (auto &b : data)
    b = static_cast<uint8_t>(dist(rng));
  return data;
}

// ---------------------------------------------------------------------------
// Pre-compressed dataset holder
// ---------------------------------------------------------------------------
struct CompressedData {
  std::vector<uint8_t> original;
  std::vector<uint8_t> compressed;
  size_t compressed_size = 0;
};

static CompressedData
prepare_compressed(std::vector<uint8_t> data,
                   gsst::Layout layout = gsst::Layout::Splits) {
  CompressedData cd;
  cd.original = std::move(data);

  gsst::Codec codec;
  gsst::CompressOptions opts;
  opts.layout = layout;

  const size_t bound = gsst::compress_bound(cd.original.size(), opts);
  cd.compressed.resize(bound);

  auto cr = codec.compress(cd.original.data(), cd.original.size(),
                           cd.compressed.data(), cd.compressed.size(), opts);
  cd.compressed_size = cr.compressed_size;
  cd.compressed.resize(cd.compressed_size);
  return cd;
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------
static void BM_DecompressRepetitive(benchmark::State &state) {
  const size_t size = static_cast<size_t>(state.range(0));
  auto cd = prepare_compressed(gen_repetitive(size));
  gsst::Codec codec;
  std::vector<uint8_t> output(cd.original.size());

  for (auto _ : state) {
    auto result = codec.decompress(cd.compressed.data(), cd.compressed_size,
                                   output.data(), output.size());
    benchmark::DoNotOptimize(result);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          cd.original.size());
  state.counters["ratio"] =
      static_cast<double>(cd.original.size()) / cd.compressed_size;
  state.SetLabel("repetitive");
}

static void BM_DecompressRandom(benchmark::State &state) {
  const size_t size = static_cast<size_t>(state.range(0));
  auto cd = prepare_compressed(gen_random(size));
  gsst::Codec codec;
  std::vector<uint8_t> output(cd.original.size());

  for (auto _ : state) {
    auto result = codec.decompress(cd.compressed.data(), cd.compressed_size,
                                   output.data(), output.size());
    benchmark::DoNotOptimize(result);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          cd.original.size());
  state.counters["ratio"] =
      static_cast<double>(cd.original.size()) / cd.compressed_size;
  state.SetLabel("random");
}

// Benchmark different layouts
static void BM_DecompressLayouts(benchmark::State &state) {
  const size_t size = 1 << 20; // 1 MiB
  auto layout = static_cast<gsst::Layout>(state.range(0));
  auto cd = prepare_compressed(gen_repetitive(size), layout);
  gsst::Codec codec;
  std::vector<uint8_t> output(cd.original.size());

  for (auto _ : state) {
    auto result = codec.decompress(cd.compressed.data(), cd.compressed_size,
                                   output.data(), output.size());
    benchmark::DoNotOptimize(result);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          cd.original.size());
  state.counters["ratio"] =
      static_cast<double>(cd.original.size()) / cd.compressed_size;
}

// Register benchmarks
BENCHMARK(BM_DecompressRepetitive)
    ->RangeMultiplier(4)
    ->Range(1 << 12, 1 << 24) // 4K to 16M
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_DecompressRandom)
    ->RangeMultiplier(4)
    ->Range(1 << 12, 1 << 24)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_DecompressLayouts)
    ->Arg(0) // Blocks
    ->Arg(1) // Splits
    ->Arg(2) // Coalesce
    ->Unit(benchmark::kMillisecond);
