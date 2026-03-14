// =============================================================================
// GSST — Compression benchmarks
// =============================================================================
#include <benchmark/benchmark.h>
#include <gsst/gsst.hpp>

#include <random>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Data generators
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
// Benchmarks
// ---------------------------------------------------------------------------
static void BM_CompressRepetitive(benchmark::State &state) {
  const size_t size = static_cast<size_t>(state.range(0));
  auto data = gen_repetitive(size);
  gsst::Codec codec;
  const size_t bound = gsst::compress_bound(size);
  std::vector<uint8_t> compressed(bound);

  for (auto _ : state) {
    auto result = codec.compress(data.data(), data.size(), compressed.data(),
                                 compressed.size());
    benchmark::DoNotOptimize(result);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * size);
  state.SetLabel("repetitive");
}

static void BM_CompressRandom(benchmark::State &state) {
  const size_t size = static_cast<size_t>(state.range(0));
  auto data = gen_random(size);
  gsst::Codec codec;
  const size_t bound = gsst::compress_bound(size);
  std::vector<uint8_t> compressed(bound);

  for (auto _ : state) {
    auto result = codec.compress(data.data(), data.size(), compressed.data(),
                                 compressed.size());
    benchmark::DoNotOptimize(result);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * size);
  state.SetLabel("random");
}

static void BM_CompressLowEntropy(benchmark::State &state) {
  const size_t size = static_cast<size_t>(state.range(0));
  auto data = gen_random(size, 10); // Only 10 distinct values
  gsst::Codec codec;
  const size_t bound = gsst::compress_bound(size);
  std::vector<uint8_t> compressed(bound);

  for (auto _ : state) {
    auto result = codec.compress(data.data(), data.size(), compressed.data(),
                                 compressed.size());
    benchmark::DoNotOptimize(result);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * size);
  state.SetLabel("low-entropy");
}

// Benchmark different layouts
static void BM_CompressLayouts(benchmark::State &state) {
  const size_t size = 1 << 20; // 1 MiB
  auto data = gen_repetitive(size);
  gsst::Codec codec;
  const size_t bound = gsst::compress_bound(size);
  std::vector<uint8_t> compressed(bound);

  gsst::CompressOptions opts;
  opts.layout = static_cast<gsst::Layout>(state.range(0));

  for (auto _ : state) {
    auto result = codec.compress(data.data(), data.size(), compressed.data(),
                                 compressed.size(), opts);
    benchmark::DoNotOptimize(result);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * size);
}

// Register benchmarks
BENCHMARK(BM_CompressRepetitive)
    ->RangeMultiplier(4)
    ->Range(1 << 12, 1 << 24) // 4K to 16M
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_CompressRandom)
    ->RangeMultiplier(4)
    ->Range(1 << 12, 1 << 24)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_CompressLowEntropy)
    ->RangeMultiplier(4)
    ->Range(1 << 12, 1 << 24)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_CompressLayouts)
    ->Arg(0) // Blocks
    ->Arg(1) // Splits
    ->Arg(2) // Coalesce
    ->Unit(benchmark::kMillisecond);
