/*
Benchmark: Tiled materialization vs sorted indirection for MI computation.

Tests:
1. Two indirected columns (sorted) — current approach
2. One materialized + one indirected — hybrid for on-the-fly path
3. Both materialized — after tiled gather
4. Tiled block computation — amortized gather over B*(B-1)/2 pairs
5. End-to-end triangular cache construction comparison
*/

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

// Simulate MI histogram building with different access patterns.
// Uses raw arrays to isolate access pattern cost from dataset overhead.

struct test_data {
  std::size_t n;
  std::size_t num_cols;
  unsigned char cardinality;
  std::vector<unsigned char> columns; // column-major: col * n + inst
  std::vector<std::size_t> sorted_indices;

  test_data(std::size_t n_, std::size_t num_cols_, unsigned char card, unsigned seed = 42)
      : n(n_), num_cols(num_cols_), cardinality(card), columns(n_ * num_cols_) {
    std::mt19937 gen(seed);
    std::uniform_int_distribution<unsigned char> dist(0, card - 1);
    for (auto &v : columns) {
      v = dist(gen);
    }
    // Bootstrap indices
    std::uniform_int_distribution<std::size_t> idist(0, n_ - 1);
    sorted_indices.resize(n_);
    for (auto &idx : sorted_indices) {
      idx = idist(gen);
    }
    std::sort(sorted_indices.begin(), sorted_indices.end());
  }

  unsigned char const *col(std::size_t c) const { return &columns[c * n]; }
};

// Histogram: two indirected columns (sorted indices)
std::size_t hist_two_indirected(test_data const &td, std::size_t c1, std::size_t c2,
                                std::vector<std::size_t> &scratch) {
  std::size_t k = td.cardinality;
  scratch.assign(k * k, 0);
  auto const *col1 = td.col(c1);
  auto const *col2 = td.col(c2);
  for (std::size_t i = 0; i < td.sorted_indices.size(); ++i) {
    std::size_t idx = td.sorted_indices[i];
    ++scratch[col1[idx] * k + col2[idx]];
  }
  return scratch[0];
}

// Histogram: one materialized column + one indirected
std::size_t hist_one_mat_one_ind(unsigned char const *mat_col, test_data const &td, std::size_t c2,
                                 std::vector<std::size_t> &scratch) {
  std::size_t k = td.cardinality;
  scratch.assign(k * k, 0);
  auto const *col2 = td.col(c2);
  for (std::size_t i = 0; i < td.sorted_indices.size(); ++i) {
    std::size_t idx = td.sorted_indices[i];
    ++scratch[mat_col[i] * k + col2[idx]];
  }
  return scratch[0];
}

// Histogram: both columns materialized (fully sequential)
std::size_t hist_both_mat(unsigned char const *mat1, unsigned char const *mat2, std::size_t n,
                          unsigned char k, std::vector<std::size_t> &scratch) {
  scratch.assign(k * k, 0);
  for (std::size_t i = 0; i < n; ++i) {
    ++scratch[static_cast<std::size_t>(mat1[i] * k + mat2[i])];
  }
  return scratch[0];
}

// Materialize a column through sorted indices
void materialize_col(unsigned char const *src, std::vector<std::size_t> const &indices,
                     unsigned char *dst) {
  for (std::size_t i = 0; i < indices.size(); ++i) {
    dst[i] = src[indices[i]];
  }
}

// ============================================================================
// Per-pair benchmarks: compare access patterns
// ============================================================================

TEST_CASE("bench: per-pair access patterns 100K", "[!benchmark][tiled]") {
  test_data td(100000, 50, 4);
  std::vector<std::size_t> scratch;
  std::vector<unsigned char> mat1(td.n), mat2(td.n);

  // Pre-materialize for the materialized benchmarks
  materialize_col(td.col(0), td.sorted_indices, mat1.data());
  materialize_col(td.col(1), td.sorted_indices, mat2.data());

  BENCHMARK_ADVANCED("two indirected (sorted)")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return hist_two_indirected(td, 0, 1, scratch); });
  };

  BENCHMARK_ADVANCED("one materialized + one indirected")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return hist_one_mat_one_ind(mat1.data(), td, 1, scratch); });
  };

  BENCHMARK_ADVANCED("both materialized (sequential)")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure(
        [&] { return hist_both_mat(mat1.data(), mat2.data(), td.n, td.cardinality, scratch); });
  };

  BENCHMARK_ADVANCED("materialize one column")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      materialize_col(td.col(2), td.sorted_indices, mat2.data());
      return mat2[0];
    });
  };
}

// ============================================================================
// Tiled triangular: simulate B-column block computation
// ============================================================================

TEST_CASE("bench: tiled triangular B=8 vs indirection, M=200, N=100K", "[!benchmark][tiled]") {
  test_data td(100000, 200, 4);
  std::vector<std::size_t> scratch;
  std::size_t M = 200;

  BENCHMARK_ADVANCED("sorted indirection: all M*(M-1)/2 pairs")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      std::size_t total = 0;
      for (std::size_t i = 0; i < M; ++i) {
        for (std::size_t j = i + 1; j < M; ++j) {
          total += hist_two_indirected(td, i, j, scratch);
        }
      }
      return total;
    });
  };

  BENCHMARK_ADVANCED("tiled B=8: materialize blocks, sequential MI")
  (Catch::Benchmark::Chronometer meter) {
    std::size_t B = 8;
    std::vector<std::vector<unsigned char>> block_cols(B, std::vector<unsigned char>(td.n));

    meter.measure([&] {
      std::size_t total = 0;
      std::size_t num_blocks = (M + B - 1) / B;

      for (std::size_t bp = 0; bp < num_blocks; ++bp) {
        std::size_t bp_start = bp * B;
        std::size_t bp_end = std::min(bp_start + B, M);
        std::size_t bp_size = bp_end - bp_start;

        // Materialize block p
        for (std::size_t k = 0; k < bp_size; ++k) {
          materialize_col(td.col(bp_start + k), td.sorted_indices, block_cols[k].data());
        }

        // Within-block pairs
        for (std::size_t i = 0; i < bp_size; ++i) {
          for (std::size_t j = i + 1; j < bp_size; ++j) {
            total += hist_both_mat(block_cols[i].data(), block_cols[j].data(), td.n, td.cardinality,
                                   scratch);
          }
        }

        // Cross-block pairs (block p vs later blocks)
        for (std::size_t bq = bp + 1; bq < num_blocks; ++bq) {
          std::size_t bq_start = bq * B;
          std::size_t bq_end = std::min(bq_start + B, M);
          std::size_t bq_size = bq_end - bq_start;

          // Materialize block q columns one at a time
          std::vector<unsigned char> q_col(td.n);
          for (std::size_t j = 0; j < bq_size; ++j) {
            materialize_col(td.col(bq_start + j), td.sorted_indices, q_col.data());
            for (std::size_t i = 0; i < bp_size; ++i) {
              total +=
                  hist_both_mat(block_cols[i].data(), q_col.data(), td.n, td.cardinality, scratch);
            }
          }
        }
      }
      return total;
    });
  };

  BENCHMARK_ADVANCED("tiled B=16: materialize blocks, sequential MI")
  (Catch::Benchmark::Chronometer meter) {
    std::size_t B = 16;
    std::vector<std::vector<unsigned char>> block_cols(B, std::vector<unsigned char>(td.n));

    meter.measure([&] {
      std::size_t total = 0;
      std::size_t num_blocks = (M + B - 1) / B;

      for (std::size_t bp = 0; bp < num_blocks; ++bp) {
        std::size_t bp_start = bp * B;
        std::size_t bp_end = std::min(bp_start + B, M);
        std::size_t bp_size = bp_end - bp_start;

        for (std::size_t k = 0; k < bp_size; ++k) {
          materialize_col(td.col(bp_start + k), td.sorted_indices, block_cols[k].data());
        }

        for (std::size_t i = 0; i < bp_size; ++i) {
          for (std::size_t j = i + 1; j < bp_size; ++j) {
            total += hist_both_mat(block_cols[i].data(), block_cols[j].data(), td.n, td.cardinality,
                                   scratch);
          }
        }

        for (std::size_t bq = bp + 1; bq < num_blocks; ++bq) {
          std::size_t bq_start = bq * B;
          std::size_t bq_end = std::min(bq_start + B, M);
          std::size_t bq_size = bq_end - bq_start;

          std::vector<unsigned char> q_col(td.n);
          for (std::size_t j = 0; j < bq_size; ++j) {
            materialize_col(td.col(bq_start + j), td.sorted_indices, q_col.data());
            for (std::size_t i = 0; i < bp_size; ++i) {
              total +=
                  hist_both_mat(block_cols[i].data(), q_col.data(), td.n, td.cardinality, scratch);
            }
          }
        }
      }
      return total;
    });
  };
}

// ============================================================================
// On-the-fly path: last_selected materialized, candidates indirected vs materialized
// ============================================================================

TEST_CASE("bench: on-the-fly path strategies, N=100K", "[!benchmark][tiled]") {
  test_data td(100000, 50, 4);
  std::vector<std::size_t> scratch;
  std::vector<unsigned char> last_mat(td.n), cand_mat(td.n);
  std::size_t num_candidates = 49; // simulate one rank with 49 remaining

  // Materialize the "last selected" column once
  materialize_col(td.col(0), td.sorted_indices, last_mat.data());

  BENCHMARK_ADVANCED("on-the-fly: two indirected per candidate")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      std::size_t total = 0;
      for (std::size_t c = 1; c <= num_candidates; ++c) {
        total += hist_two_indirected(td, 0, c, scratch);
      }
      return total;
    });
  };

  BENCHMARK_ADVANCED("on-the-fly: last_mat + candidate indirected")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      std::size_t total = 0;
      for (std::size_t c = 1; c <= num_candidates; ++c) {
        total += hist_one_mat_one_ind(last_mat.data(), td, c, scratch);
      }
      return total;
    });
  };

  BENCHMARK_ADVANCED("on-the-fly: last_mat + candidate materialized each")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      std::size_t total = 0;
      for (std::size_t c = 1; c <= num_candidates; ++c) {
        materialize_col(td.col(c), td.sorted_indices, cand_mat.data());
        total += hist_both_mat(last_mat.data(), cand_mat.data(), td.n, td.cardinality, scratch);
      }
      return total;
    });
  };
}
