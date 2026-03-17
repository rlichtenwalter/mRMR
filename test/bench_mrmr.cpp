/*
Copyright (C) 2018 by Ryan N. Lichtenwalter
Email: rlichtenwalter@gmail.com

This file is part of the Improved mRMR code base.

Improved mRMR is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Improved mRMR is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <mrmr/dataset.hpp>
#include <mrmr/mrmr.hpp>

char DELIMITER = '\t';

// --- Synthetic data generation ---

// Generate a random integer dataset suitable for mRMR.
// Values are in [0, cardinality) so no discretization is needed.
// Returns row-major data vector with num_instances * num_attributes elements.
std::vector<unsigned char> generate_random_data(std::size_t num_instances,
                                                std::size_t num_attributes,
                                                unsigned char cardinality = 4, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_int_distribution<unsigned char> dist(0, cardinality - 1);
  std::vector<unsigned char> data(num_instances * num_attributes);
  for (auto &val : data) {
    val = dist(gen);
  }
  return data;
}

// Build a dataset from synthetic data.
dataset<unsigned char> build_dataset(std::size_t num_instances, std::size_t num_attributes,
                                     unsigned char cardinality = 4, unsigned seed = 42) {
  auto data = generate_random_data(num_instances, num_attributes, cardinality, seed);
  return dataset<unsigned char>(data, num_instances, num_attributes, false, {},
                                dataset<unsigned char>::ROUND);
}

// ============================================================
// Dataset construction benchmarks
// ============================================================

TEST_CASE("bench: dataset construction", "[!benchmark][construction]") {
  BENCHMARK_ADVANCED("1K instances, 20 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_random_data(1000, 20, 4);
    meter.measure([&] {
      return dataset<unsigned char>(data, 1000, 20, false, {}, dataset<unsigned char>::ROUND);
    });
  };

  BENCHMARK_ADVANCED("10K instances, 20 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_random_data(10000, 20, 4);
    meter.measure([&] {
      return dataset<unsigned char>(data, 10000, 20, false, {}, dataset<unsigned char>::ROUND);
    });
  };

  BENCHMARK_ADVANCED("10K instances, 50 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_random_data(10000, 50, 4);
    meter.measure([&] {
      return dataset<unsigned char>(data, 10000, 50, false, {}, dataset<unsigned char>::ROUND);
    });
  };

  BENCHMARK_ADVANCED("10K instances, 200 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_random_data(10000, 200, 4);
    meter.measure([&] {
      return dataset<unsigned char>(data, 10000, 200, false, {}, dataset<unsigned char>::ROUND);
    });
  };

  BENCHMARK_ADVANCED("100K instances, 50 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_random_data(100000, 50, 4);
    meter.measure([&] {
      return dataset<unsigned char>(data, 100000, 50, false, {}, dataset<unsigned char>::ROUND);
    });
  };
}

// ============================================================
// Mutual information benchmarks
// ============================================================

TEST_CASE("bench: mutual information", "[!benchmark][mutual-information]") {
  auto ds_small = build_dataset(1000, 20, 4);

  BENCHMARK_ADVANCED("1K instances, card=4, single pair")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return ds_small.mutual_information(0, 1); });
  };

  auto ds_medium = build_dataset(10000, 20, 4);

  BENCHMARK_ADVANCED("10K instances, card=4, single pair")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return ds_medium.mutual_information(0, 1); });
  };

  auto ds_highcard = build_dataset(10000, 20, 50);

  BENCHMARK_ADVANCED("10K instances, card=50, single pair")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return ds_highcard.mutual_information(0, 1); });
  };

  auto ds_large = build_dataset(100000, 20, 4);

  BENCHMARK_ADVANCED("100K instances, card=4, single pair")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return ds_large.mutual_information(0, 1); });
  };

  BENCHMARK_ADVANCED("10K instances, card=4, all pairs with class")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] {
      double sum = 0;
      for (std::size_t i = 1; i < ds_medium.num_attributes(); ++i) {
        sum += ds_medium.mutual_information(0, i);
      }
      return sum;
    });
  };
}

// ============================================================
// Full mRMR feature selection benchmarks
// ============================================================

TEST_CASE("bench: mRMR feature selection", "[!benchmark][feature-selection]") {
  auto ds_small = build_dataset(1000, 20, 4);

  BENCHMARK_ADVANCED("1K instances, 20 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_small, 0); });
  };

  auto ds_medium = build_dataset(10000, 50, 4);

  BENCHMARK_ADVANCED("10K instances, 50 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_medium, 0); });
  };

  auto ds_wide = build_dataset(10000, 200, 4);

  BENCHMARK_ADVANCED("10K instances, 200 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_wide, 0); });
  };

  auto ds_large = build_dataset(100000, 50, 4);

  BENCHMARK_ADVANCED("100K instances, 50 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_large, 0); });
  };

  auto ds_highcard = build_dataset(10000, 50, 50);

  BENCHMARK_ADVANCED("10K instances, 50 attrs, card=50")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_highcard, 0); });
  };
}

// ============================================================
// Cardinality scaling benchmarks
// ============================================================

TEST_CASE("bench: cardinality scaling", "[!benchmark][cardinality]") {
  auto ds_card2 = build_dataset(10000, 50, 2);

  BENCHMARK_ADVANCED("10K instances, 50 attrs, card=2")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_card2, 0); });
  };

  auto ds_card4 = build_dataset(10000, 50, 4);

  BENCHMARK_ADVANCED("10K instances, 50 attrs, card=4")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_card4, 0); });
  };

  auto ds_card10 = build_dataset(10000, 50, 10);

  BENCHMARK_ADVANCED("10K instances, 50 attrs, card=10")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_card10, 0); });
  };

  auto ds_card50 = build_dataset(10000, 50, 50);

  BENCHMARK_ADVANCED("10K instances, 50 attrs, card=50")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_card50, 0); });
  };

  auto ds_card200 = build_dataset(10000, 50, 200);

  BENCHMARK_ADVANCED("10K instances, 50 attrs, card=200")
  (Catch::Benchmark::Chronometer meter) {
    meter.measure([&] { return mrmr(ds_card200, 0); });
  };
}
