// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2018-2026 Ryan N. Lichtenwalter

/*
Benchmark: KSG MI estimation and continuous_dataset performance.

Compares:
1. KSG MI (continuous) vs histogram MI (discrete) at various N
2. Ross mixed MI performance
3. continuous_dataset full mRMR ranking
*/

#include <cstddef>
#include <random>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#ifdef MRMR_HAS_CONTINUOUS
#include <mrmr/continuous_dataset.hpp>
#include <mrmr/dataset.hpp>
#include <mrmr/ksg_estimator.hpp>
#include <mrmr/mixed_dataset.hpp>
#include <mrmr/mrmr.hpp>

// Generate correlated Gaussian data
std::vector<double> generate_gaussian_data(std::size_t n, std::size_t attrs, double rho = 0.5,
                                           unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::normal_distribution<double> norm(0.0, 1.0);
  std::vector<double> data(n * attrs);
  for (std::size_t i = 0; i < n; ++i) {
    double z = norm(gen);
    data[i * attrs + 0] = (i < n / 2) ? 0.0 : 1.0; // class
    for (std::size_t a = 1; a < attrs; ++a) {
      double noise = norm(gen);
      data[i * attrs + a] = rho * z + std::sqrt(1.0 - rho * rho) * noise;
    }
  }
  return data;
}

TEST_CASE("bench: KSG MI at various N", "[!benchmark][continuous]") {
  BENCHMARK_ADVANCED("KSG MI N=1K")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_gaussian_data(1000, 3);
    continuous_dataset<double> ds(data, 1000, 3, {"c", "x", "y"});
    meter.measure([&] { return ds.mutual_information(1, 2); });
  };

  BENCHMARK_ADVANCED("KSG MI N=5K")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_gaussian_data(5000, 3);
    continuous_dataset<double> ds(data, 5000, 3, {"c", "x", "y"});
    meter.measure([&] { return ds.mutual_information(1, 2); });
  };

  BENCHMARK_ADVANCED("KSG MI N=10K")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_gaussian_data(10000, 3);
    continuous_dataset<double> ds(data, 10000, 3, {"c", "x", "y"});
    meter.measure([&] { return ds.mutual_information(1, 2); });
  };
}

TEST_CASE("bench: discrete MI vs KSG MI at N=10K", "[!benchmark][continuous]") {
  // Discrete MI (histogram)
  BENCHMARK_ADVANCED("discrete MI N=10K card=4")
  (Catch::Benchmark::Chronometer meter) {
    std::mt19937 gen(42);
    std::uniform_int_distribution<unsigned char> dist(0, 3);
    std::vector<unsigned char> data(10000 * 3);
    for (auto &v : data) {
      v = dist(gen);
    }
    dataset<unsigned char> ds(data, 10000, 3);
    meter.measure([&] { return ds.mutual_information(1, 2); });
  };

  // KSG MI (continuous)
  BENCHMARK_ADVANCED("KSG MI N=10K")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_gaussian_data(10000, 3);
    continuous_dataset<double> ds(data, 10000, 3, {"c", "x", "y"});
    meter.measure([&] { return ds.mutual_information(1, 2); });
  };
}

TEST_CASE("bench: full mRMR on continuous_dataset", "[!benchmark][continuous]") {
  BENCHMARK_ADVANCED("continuous mRMR N=500 M=10")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_gaussian_data(500, 10);
    std::vector<std::string> names = {"class"};
    for (int i = 1; i < 10; ++i) {
      names.push_back("f" + std::to_string(i));
    }
    continuous_dataset<double> ds(data, 500, 10, names);
    meter.measure([&] { return mrmr(ds, 0); });
  };

  BENCHMARK_ADVANCED("continuous mRMR N=1K M=20")
  (Catch::Benchmark::Chronometer meter) {
    auto data = generate_gaussian_data(1000, 20);
    std::vector<std::string> names = {"class"};
    for (int i = 1; i < 20; ++i) {
      names.push_back("f" + std::to_string(i));
    }
    continuous_dataset<double> ds(data, 1000, 20, names);
    meter.measure([&] { return mrmr(ds, 0); });
  };
}

TEST_CASE("bench: mixed_dataset MI dispatch", "[!benchmark][continuous]") {
  BENCHMARK_ADVANCED("mixed DD pair N=10K")
  (Catch::Benchmark::Chronometer meter) {
    std::vector<column_type> types = {column_type::DISCRETE, column_type::DISCRETE,
                                      column_type::DISCRETE};
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> dist(0, 3);
    std::vector<double> data(10000 * 3);
    for (auto &v : data) {
      v = dist(gen);
    }
    mixed_dataset ds(types, data, 10000, 3);
    meter.measure([&] { return ds.mutual_information(0, 1); });
  };

  BENCHMARK_ADVANCED("mixed CC pair N=10K")
  (Catch::Benchmark::Chronometer meter) {
    std::vector<column_type> types = {column_type::CONTINUOUS, column_type::CONTINUOUS,
                                      column_type::CONTINUOUS};
    auto data = generate_gaussian_data(10000, 3, 0.5, 123);
    mixed_dataset ds(types, data, 10000, 3);
    meter.measure([&] { return ds.mutual_information(0, 1); });
  };

  BENCHMARK_ADVANCED("mixed DC pair N=10K")
  (Catch::Benchmark::Chronometer meter) {
    std::vector<column_type> types = {column_type::DISCRETE, column_type::CONTINUOUS,
                                      column_type::CONTINUOUS};
    std::mt19937 gen(42);
    std::vector<double> data(10000 * 3);
    for (std::size_t i = 0; i < 10000; ++i) {
      data[i * 3 + 0] = (i % 4);
      data[i * 3 + 1] = std::normal_distribution<double>(data[i * 3 + 0], 1.0)(gen);
      data[i * 3 + 2] = std::normal_distribution<double>(0.0, 1.0)(gen);
    }
    mixed_dataset ds(types, data, 10000, 3);
    meter.measure([&] { return ds.mutual_information(0, 1); });
  };
}

#endif // MRMR_HAS_CONTINUOUS
