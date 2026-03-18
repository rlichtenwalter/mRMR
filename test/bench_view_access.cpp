/*
Benchmark: Compare dataset access patterns for view-based MI computation.

Tests three approaches:
1. Direct access (baseline) — sequential stride-1 scan, no indirection
2. Sorted index indirection — access through sorted bootstrap index vector
3. Unsorted index indirection — access through random bootstrap index vector

Each approach builds a joint histogram (the MI hot loop) for a pair of attributes.
This isolates the data access cost from the MI computation itself.
*/

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <mrmr/dataset.hpp>

// Generate a random dataset
dataset<unsigned char> make_test_dataset(std::size_t num_instances, std::size_t num_attributes,
                                         unsigned char cardinality = 4, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<unsigned char> dist(0, cardinality - 1);
  std::vector<unsigned char> data(num_instances * num_attributes);
  for (auto &val : data) {
    val = dist(gen);
  }
  return dataset<unsigned char>(data, num_instances, num_attributes, false, {},
                                dataset<unsigned char>::ROUND);
}

// Generate bootstrap indices (with replacement)
std::vector<std::size_t> make_bootstrap_indices(std::size_t n, unsigned seed = 123) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<std::size_t> dist(0, n - 1);
  std::vector<std::size_t> indices(n);
  for (auto &idx : indices) {
    idx = dist(gen);
  }
  return indices;
}

// Histogram building via direct sequential access (baseline)
std::size_t histogram_direct(dataset<unsigned char> const &ds, std::size_t attr1, std::size_t attr2,
                             std::vector<std::size_t> &scratch) {
  std::size_t k1 = ds.attribute_entropy(attr1) > 0 ? 4 : 1; // approximate cardinality
  std::size_t k2 = ds.attribute_entropy(attr2) > 0 ? 4 : 1;
  scratch.assign(k1 * k2, 0);
  // Access raw data through the matrix — sequential stride-1
  for (std::size_t i = 0; i < ds.num_instances(); ++i) {
    // Simulate direct access: ds._data(attr, i) would be sequential
    // We use mutual_information as proxy since we can't access _data directly
  }
  // Use the actual MI function as the benchmark target
  return static_cast<std::size_t>(ds.mutual_information(attr1, attr2) * 1e9);
}

// ============================================================================
// Benchmarks
// ============================================================================

TEST_CASE("bench: MI computation baseline (direct access)", "[!benchmark][view-access]") {
  BENCHMARK_ADVANCED("10K inst, 50 attr, direct MI")
  (Catch::Benchmark::Chronometer meter) {
    auto ds = make_test_dataset(10000, 50, 4);
    meter.measure([&] { return ds.mutual_information(0, 1); });
  };

  BENCHMARK_ADVANCED("100K inst, 50 attr, direct MI")
  (Catch::Benchmark::Chronometer meter) {
    auto ds = make_test_dataset(100000, 50, 4);
    meter.measure([&] { return ds.mutual_information(0, 1); });
  };

  BENCHMARK_ADVANCED("10K inst, 50 attr, card=50, direct MI")
  (Catch::Benchmark::Chronometer meter) {
    auto ds = make_test_dataset(10000, 50, 50);
    meter.measure([&] { return ds.mutual_information(0, 1); });
  };
}

TEST_CASE("bench: histogram via sorted index indirection", "[!benchmark][view-access]") {
  // Simulate what a dataset_view would do: access data through sorted indices
  auto ds = make_test_dataset(10000, 50, 4);
  auto indices_unsorted = make_bootstrap_indices(10000);
  auto indices_sorted = indices_unsorted;
  std::sort(indices_sorted.begin(), indices_sorted.end());

  // We can't access ds._data directly, so we benchmark the full MI overhead
  // by comparing sorted vs unsorted index patterns on raw array access.

  // Create column data to simulate direct access patterns
  std::vector<unsigned char> col1(10000), col2(10000);
  {
    std::mt19937 gen(42);
    std::uniform_int_distribution<unsigned char> dist(0, 3);
    for (auto &v : col1)
      v = dist(gen);
    for (auto &v : col2)
      v = dist(gen);
  }

  std::vector<std::size_t> scratch(16, 0);

  BENCHMARK_ADVANCED("10K: direct sequential histogram")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      scratch.assign(16, 0);
      for (std::size_t i = 0; i < 10000; ++i) {
        ++scratch[col1[i] * 4 + col2[i]];
      }
      return scratch[0];
    });
  };

  BENCHMARK_ADVANCED("10K: sorted index histogram")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      scratch.assign(16, 0);
      for (std::size_t i = 0; i < indices_sorted.size(); ++i) {
        std::size_t idx = indices_sorted[i];
        ++scratch[col1[idx] * 4 + col2[idx]];
      }
      return scratch[0];
    });
  };

  BENCHMARK_ADVANCED("10K: unsorted index histogram")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      scratch.assign(16, 0);
      for (std::size_t i = 0; i < indices_unsorted.size(); ++i) {
        std::size_t idx = indices_unsorted[i];
        ++scratch[col1[idx] * 4 + col2[idx]];
      }
      return scratch[0];
    });
  };
}

TEST_CASE("bench: sorted vs unsorted at 100K instances", "[!benchmark][view-access]") {
  std::vector<unsigned char> col1(100000), col2(100000);
  {
    std::mt19937 gen(42);
    std::uniform_int_distribution<unsigned char> dist(0, 3);
    for (auto &v : col1)
      v = dist(gen);
    for (auto &v : col2)
      v = dist(gen);
  }

  auto indices_unsorted = make_bootstrap_indices(100000);
  auto indices_sorted = indices_unsorted;
  std::sort(indices_sorted.begin(), indices_sorted.end());

  std::vector<std::size_t> scratch(16, 0);

  BENCHMARK_ADVANCED("100K: direct sequential")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      scratch.assign(16, 0);
      for (std::size_t i = 0; i < 100000; ++i) {
        ++scratch[col1[i] * 4 + col2[i]];
      }
      return scratch[0];
    });
  };

  BENCHMARK_ADVANCED("100K: sorted index")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      scratch.assign(16, 0);
      for (std::size_t i = 0; i < indices_sorted.size(); ++i) {
        std::size_t idx = indices_sorted[i];
        ++scratch[col1[idx] * 4 + col2[idx]];
      }
      return scratch[0];
    });
  };

  BENCHMARK_ADVANCED("100K: unsorted index")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      scratch.assign(16, 0);
      for (std::size_t i = 0; i < indices_unsorted.size(); ++i) {
        std::size_t idx = indices_unsorted[i];
        ++scratch[col1[idx] * 4 + col2[idx]];
      }
      return scratch[0];
    });
  };
}

TEST_CASE("bench: column materialization cost", "[!benchmark][view-access]") {
  // Measure the cost of materializing one column through sorted indices
  std::vector<unsigned char> source_col(100000);
  {
    std::mt19937 gen(42);
    std::uniform_int_distribution<unsigned char> dist(0, 3);
    for (auto &v : source_col)
      v = dist(gen);
  }

  auto indices_sorted = make_bootstrap_indices(100000);
  std::sort(indices_sorted.begin(), indices_sorted.end());

  std::vector<unsigned char> materialized(100000);

  BENCHMARK_ADVANCED("100K: materialize one column (sorted gather)")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      for (std::size_t i = 0; i < indices_sorted.size(); ++i) {
        materialized[i] = source_col[indices_sorted[i]];
      }
      return materialized[0];
    });
  };

  BENCHMARK_ADVANCED("100K: sort 100K indices")
  (Catch::Benchmark::Chronometer meter) {
    auto to_sort = make_bootstrap_indices(100000);
    meter.measure([&] {
      std::sort(to_sort.begin(), to_sort.end());
      return to_sort[0];
    });
  };
}

TEST_CASE("bench: high cardinality impact", "[!benchmark][view-access]") {
  // Test with cardinality=50 (larger histogram)
  std::vector<unsigned char> col1(100000), col2(100000);
  {
    std::mt19937 gen(42);
    std::uniform_int_distribution<unsigned char> dist(0, 49);
    for (auto &v : col1)
      v = dist(gen);
    for (auto &v : col2)
      v = dist(gen);
  }

  auto indices_sorted = make_bootstrap_indices(100000);
  std::sort(indices_sorted.begin(), indices_sorted.end());

  std::vector<std::size_t> scratch(2500, 0); // 50*50

  BENCHMARK_ADVANCED("100K card=50: direct sequential")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      scratch.assign(2500, 0);
      for (std::size_t i = 0; i < 100000; ++i) {
        ++scratch[col1[i] * 50 + col2[i]];
      }
      return scratch[0];
    });
  };

  BENCHMARK_ADVANCED("100K card=50: sorted index")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      scratch.assign(2500, 0);
      for (std::size_t i = 0; i < indices_sorted.size(); ++i) {
        std::size_t idx = indices_sorted[i];
        ++scratch[col1[idx] * 50 + col2[idx]];
      }
      return scratch[0];
    });
  };
}

TEST_CASE("bench: full MI via dataset (end-to-end)", "[!benchmark][view-access]") {
  // End-to-end MI computation through the actual dataset API
  // This captures the real cost including marginal probability lookups and log2

  BENCHMARK_ADVANCED("10K inst, MI via dataset, card=4")
  (Catch::Benchmark::Chronometer meter) {
    auto ds = make_test_dataset(10000, 50, 4);
    meter.measure([&] { return ds.mutual_information(1, 2); });
  };

  BENCHMARK_ADVANCED("100K inst, MI via dataset, card=4")
  (Catch::Benchmark::Chronometer meter) {
    auto ds = make_test_dataset(100000, 50, 4);
    meter.measure([&] { return ds.mutual_information(1, 2); });
  };

  BENCHMARK_ADVANCED("100K inst, MI via dataset, card=50")
  (Catch::Benchmark::Chronometer meter) {
    auto ds = make_test_dataset(100000, 50, 50);
    meter.measure([&] { return ds.mutual_information(1, 2); });
  };
}
