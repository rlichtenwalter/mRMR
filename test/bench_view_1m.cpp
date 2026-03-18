/*
Benchmark: Access pattern comparison at 1M instances.

At N=1M, each column is 1MB (unsigned char). L2 cache (~256KB-1MB)
can barely hold one column. This tests whether materialization
benefits emerge at scale where cache pressure is highest.

Key scenarios favorable to materialization:
- Large N where sorted indirection causes more L2/L3 misses
- High column reuse within tiled blocks
- Higher cardinality (larger histograms compete for cache with data)
*/

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <random>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

struct large_test_data {
  std::size_t n;
  std::size_t num_cols;
  unsigned char cardinality;
  std::vector<unsigned char> columns;
  std::vector<std::size_t> sorted_indices;
  std::vector<std::size_t> unsorted_indices;

  large_test_data(std::size_t n_, std::size_t num_cols_, unsigned char card, unsigned seed = 42)
      : n(n_), num_cols(num_cols_), cardinality(card), columns(n_ * num_cols_) {
    std::mt19937 gen(seed);
    std::uniform_int_distribution<unsigned char> dist(0, card - 1);
    for (auto &v : columns) {
      v = dist(gen);
    }
    std::uniform_int_distribution<std::size_t> idist(0, n_ - 1);
    unsorted_indices.resize(n_);
    for (auto &idx : unsorted_indices) {
      idx = idist(gen);
    }
    sorted_indices = unsorted_indices;
    std::sort(sorted_indices.begin(), sorted_indices.end());
  }

  unsigned char const *col(std::size_t c) const { return &columns[c * n]; }
};

std::size_t hist_direct(unsigned char const *c1, unsigned char const *c2, std::size_t n,
                        unsigned char k, std::vector<std::size_t> &scratch) {
  scratch.assign(k * k, 0);
  for (std::size_t i = 0; i < n; ++i) {
    ++scratch[c1[i] * k + c2[i]];
  }
  return scratch[0];
}

std::size_t hist_sorted_ind(unsigned char const *c1, unsigned char const *c2,
                            std::vector<std::size_t> const &indices, unsigned char k,
                            std::vector<std::size_t> &scratch) {
  scratch.assign(k * k, 0);
  for (std::size_t i = 0; i < indices.size(); ++i) {
    std::size_t idx = indices[i];
    ++scratch[c1[idx] * k + c2[idx]];
  }
  return scratch[0];
}

void gather_col(unsigned char const *src, std::vector<std::size_t> const &indices,
                unsigned char *dst) {
  for (std::size_t i = 0; i < indices.size(); ++i) {
    dst[i] = src[indices[i]];
  }
}

// ============================================================================
// Per-pair: 1M instances, card=4
// ============================================================================

TEST_CASE("bench: 1M instances, card=4, per-pair", "[!benchmark][1m]") {
  large_test_data td(1000000, 20, 4);
  std::vector<std::size_t> scratch;
  std::vector<unsigned char> mat1(td.n), mat2(td.n);

  gather_col(td.col(0), td.sorted_indices, mat1.data());
  gather_col(td.col(1), td.sorted_indices, mat2.data());

  BENCHMARK_ADVANCED("direct sequential")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return hist_direct(td.col(0), td.col(1), td.n, td.cardinality, scratch); });
  };

  BENCHMARK_ADVANCED("sorted indirection")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      return hist_sorted_ind(td.col(0), td.col(1), td.sorted_indices, td.cardinality, scratch);
    });
  };

  BENCHMARK_ADVANCED("both materialized")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure(
        [&] { return hist_direct(mat1.data(), mat2.data(), td.n, td.cardinality, scratch); });
  };

  BENCHMARK_ADVANCED("materialize one column")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      gather_col(td.col(2), td.sorted_indices, mat2.data());
      return mat2[0];
    });
  };

  BENCHMARK_ADVANCED("gather + sequential MI (per pair)")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      gather_col(td.col(1), td.sorted_indices, mat2.data());
      return hist_direct(mat1.data(), mat2.data(), td.n, td.cardinality, scratch);
    });
  };
}

// ============================================================================
// Per-pair: 1M instances, card=50 (larger histogram, more cache pressure)
// ============================================================================

TEST_CASE("bench: 1M instances, card=50, per-pair", "[!benchmark][1m]") {
  large_test_data td(1000000, 20, 50);
  std::vector<std::size_t> scratch;
  std::vector<unsigned char> mat1(td.n), mat2(td.n);

  gather_col(td.col(0), td.sorted_indices, mat1.data());
  gather_col(td.col(1), td.sorted_indices, mat2.data());

  BENCHMARK_ADVANCED("direct sequential card=50")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return hist_direct(td.col(0), td.col(1), td.n, td.cardinality, scratch); });
  };

  BENCHMARK_ADVANCED("sorted indirection card=50")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      return hist_sorted_ind(td.col(0), td.col(1), td.sorted_indices, td.cardinality, scratch);
    });
  };

  BENCHMARK_ADVANCED("both materialized card=50")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure(
        [&] { return hist_direct(mat1.data(), mat2.data(), td.n, td.cardinality, scratch); });
  };

  BENCHMARK_ADVANCED("gather + sequential card=50")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      gather_col(td.col(1), td.sorted_indices, mat2.data());
      return hist_direct(mat1.data(), mat2.data(), td.n, td.cardinality, scratch);
    });
  };
}

// ============================================================================
// Tiled block: B=8, M=50, N=1M (materialization most favorable scenario)
// ============================================================================

TEST_CASE("bench: tiled B=8 vs indirection, M=50, N=1M", "[!benchmark][1m]") {
  large_test_data td(1000000, 50, 4);
  std::vector<std::size_t> scratch;
  std::size_t M = 50;

  BENCHMARK_ADVANCED("sorted indirection: all M*(M-1)/2 pairs")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      std::size_t total = 0;
      for (std::size_t i = 0; i < M; ++i) {
        for (std::size_t j = i + 1; j < M; ++j) {
          total +=
              hist_sorted_ind(td.col(i), td.col(j), td.sorted_indices, td.cardinality, scratch);
        }
      }
      return total;
    });
  };

  BENCHMARK_ADVANCED("tiled B=8: materialize + sequential")
  (Catch::Benchmark::Chronometer meter) {
    std::size_t B = 8;
    std::vector<std::vector<unsigned char>> block(B, std::vector<unsigned char>(td.n));

    meter.measure([&] {
      std::size_t total = 0;
      std::size_t num_blocks = (M + B - 1) / B;

      for (std::size_t bp = 0; bp < num_blocks; ++bp) {
        std::size_t bp_start = bp * B;
        std::size_t bp_end = std::min(bp_start + B, M);
        std::size_t bp_size = bp_end - bp_start;

        for (std::size_t k = 0; k < bp_size; ++k) {
          gather_col(td.col(bp_start + k), td.sorted_indices, block[k].data());
        }

        // Within-block pairs (fully sequential, high reuse)
        for (std::size_t i = 0; i < bp_size; ++i) {
          for (std::size_t j = i + 1; j < bp_size; ++j) {
            total += hist_direct(block[i].data(), block[j].data(), td.n, td.cardinality, scratch);
          }
        }

        // Cross-block pairs
        std::vector<unsigned char> q_col(td.n);
        for (std::size_t bq = bp + 1; bq < num_blocks; ++bq) {
          std::size_t bq_start = bq * B;
          std::size_t bq_end = std::min(bq_start + B, M);
          std::size_t bq_size = bq_end - bq_start;

          for (std::size_t j = 0; j < bq_size; ++j) {
            gather_col(td.col(bq_start + j), td.sorted_indices, q_col.data());
            for (std::size_t i = 0; i < bp_size; ++i) {
              total += hist_direct(block[i].data(), q_col.data(), td.n, td.cardinality, scratch);
            }
          }
        }
      }
      return total;
    });
  };
}

// ============================================================================
// Sort cost at 1M
// ============================================================================

TEST_CASE("bench: sort 1M indices", "[!benchmark][1m]") {
  BENCHMARK_ADVANCED("sort 1M size_t indices")
  (Catch::Benchmark::Chronometer meter) {
    std::mt19937 gen(42);
    std::uniform_int_distribution<std::size_t> dist(0, 999999);
    std::vector<std::size_t> indices(1000000);
    for (auto &idx : indices) {
      idx = dist(gen);
    }
    meter.measure([&] {
      std::sort(indices.begin(), indices.end());
      return indices[0];
    });
  };
}
