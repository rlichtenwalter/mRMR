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

#include <array>
#include <cmath>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <mrmr/attribute_information.hpp>
#include <mrmr/dataset.hpp>
#include <mrmr/dataset_view.hpp>
#include <mrmr/matrix.hpp>
#include <mrmr/missing.hpp>
#include <mrmr/mrmr.hpp>
#ifdef MRMR_HAS_CONTINUOUS
#include <mrmr/continuous_dataset.hpp>
#include <mrmr/mixed_dataset.hpp>
#endif
#include <mrmr/mrmre.hpp>

// ============================================================================
// attribute_information tests
// ============================================================================

TEST_CASE("attribute_information num_values", "[attribute_information]") {
  using value = unsigned char;
  std::array<value, 16> a = {0, 0, 0, 1, 1, 1, 0, 2, 2, 2, 1, 1, 0, 1, 1, 2};
  attribute_information<value> ai(std::cbegin(a), std::cend(a));
  REQUIRE(ai.num_values() == 3);
}

TEST_CASE("attribute_information entropy", "[attribute_information]") {
  using value = unsigned char;
  std::array<value, 16> a = {0, 0, 0, 1, 1, 1, 0, 2, 2, 2, 1, 1, 0, 1, 1, 2};
  attribute_information<value> ai(std::cbegin(a), std::cend(a));
  REQUIRE_THAT(ai.entropy(), Catch::Matchers::WithinRel(1.546179691947, 1e-10));
}

TEST_CASE("attribute_information marginal_probability", "[attribute_information]") {
  using value = unsigned char;
  std::array<value, 16> a = {0, 0, 0, 1, 1, 1, 0, 2, 2, 2, 1, 1, 0, 1, 1, 2};
  attribute_information<value> ai(std::cbegin(a), std::cend(a));
  REQUIRE(ai.marginal_probability(0) == 5.0 / 16.0);
  REQUIRE(ai.marginal_probability(1) == 7.0 / 16.0);
  REQUIRE(ai.marginal_probability(2) == 4.0 / 16.0);
}

// ============================================================================
// matrix tests
// ============================================================================

TEST_CASE("matrix element access", "[matrix]") {
  matrix<double> m(2, 3);
  m(0, 0) = 0.0;
  m(0, 1) = 0.1;
  m(0, 2) = 0.2;
  m(1, 0) = 1.0;
  m(1, 1) = 1.1;
  m(1, 2) = 1.2;

  REQUIRE(m(0, 0) == 0.0);
  REQUIRE(m(0, 1) == 0.1);
  REQUIRE(m(0, 2) == 0.2);
  REQUIRE(m(1, 0) == 1.0);
  REQUIRE(m(1, 1) == 1.1);
  REQUIRE(m(1, 2) == 1.2);
}

TEST_CASE("matrix output operator", "[matrix]") {
  matrix<double> m(2, 3);
  m(0, 0) = 0.0;
  m(0, 1) = 0.1;
  m(0, 2) = 0.2;
  m(1, 0) = 1.0;
  m(1, 1) = 1.1;
  m(1, 2) = 1.2;

  std::stringstream ss;
  ss << m;
  REQUIRE(ss.str() == "0\t0.1\t0.2\n1\t1.1\t1.2\n");
}

TEST_CASE("matrix input operator", "[matrix]") {
  matrix<double> m(2, 3);
  m(0, 0) = 0.0;
  m(0, 1) = 0.1;
  m(0, 2) = 0.2;
  m(1, 0) = 1.0;
  m(1, 1) = 1.1;
  m(1, 2) = 1.2;

  std::stringstream ss;
  ss << m;
  matrix<double> n;
  ss >> n;
  REQUIRE(m == n);
}

TEST_CASE("matrix transpose", "[matrix]") {
  matrix<double> m(2, 3);
  m(0, 0) = 0.0;
  m(0, 1) = 0.1;
  m(0, 2) = 0.2;
  m(1, 0) = 1.0;
  m(1, 1) = 1.1;
  m(1, 2) = 1.2;

  matrix<double> t = m.transpose();
  REQUIRE(t(0, 0) == 0.0);
  REQUIRE(t(0, 1) == 1.0);
  REQUIRE(t(1, 0) == 0.1);
  REQUIRE(t(1, 1) == 1.1);
  REQUIRE(t(2, 0) == 0.2);
  REQUIRE(t(2, 1) == 1.2);
}

// ============================================================================
// dataset tests
// ============================================================================

TEST_CASE("dataset I/O roundtrip", "[dataset]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream input_ss(str);
  dataset<unsigned char> ds(input_ss, dataset<unsigned char>::ROUND);

  std::stringstream output_ss;
  output_ss << ds;
  REQUIRE(str == output_ss.str());
}

TEST_CASE("dataset attribute_entropy", "[dataset]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream input_ss(str);
  dataset<unsigned char> ds(input_ss, dataset<unsigned char>::ROUND);

  REQUIRE(ds.attribute_entropy(0) == 1.0);
  REQUIRE(ds.attribute_entropy(1) == 1.0);
  REQUIRE_THAT(ds.attribute_entropy(2), Catch::Matchers::WithinRel(0.650022421648, 1e-10));
}

TEST_CASE("dataset mutual_information", "[dataset]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream input_ss(str);
  dataset<unsigned char> ds(input_ss, dataset<unsigned char>::ROUND);

  REQUIRE_THAT(ds.mutual_information(0, 1), Catch::Matchers::WithinRel(0.0817042, 1e-5));
  REQUIRE_THAT(ds.mutual_information(0, 2), Catch::Matchers::WithinRel(0.1908745, 1e-5));
}

// ============================================================================
// mRMR algorithm tests
// ============================================================================

TEST_CASE("mrmr cached vs on-the-fly produce identical rankings", "[mrmr]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream ss1(str);
  dataset<unsigned char> ds(ss1, dataset<unsigned char>::ROUND);

  // Run with cache (threshold high enough to include all attributes)
  auto result_cached = mrmr(ds, 0, nullptr, 10000);
  // Run without cache (threshold = 0 forces on-the-fly)
  auto result_onthefly = mrmr(ds, 0, nullptr, 0);

  REQUIRE(std::get<0>(result_cached) == std::get<0>(result_onthefly));
  REQUIRE(std::get<1>(result_cached) == std::get<1>(result_onthefly));
  REQUIRE(std::get<2>(result_cached) == std::get<2>(result_onthefly));
  for (std::size_t i = 0; i < std::get<5>(result_cached).size(); ++i) {
    if (std::isnan(std::get<5>(result_cached)[i])) {
      REQUIRE(std::isnan(std::get<5>(result_onthefly)[i]));
    } else {
      REQUIRE(std::get<5>(result_cached)[i] == std::get<5>(result_onthefly)[i]);
    }
  }
}

TEST_CASE("mrmr with all-constant attributes", "[mrmr]") {
  // All non-class attributes have zero entropy (constant values)
  std::vector<unsigned char> data = {0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1};
  dataset<unsigned char> ds(data, 4, 3, false, {"class", "const1", "const2"},
                            dataset<unsigned char>::ROUND);

  auto result = mrmr(ds, 0);
  auto &ranks = std::get<0>(result);
  auto &indices = std::get<1>(result);

  // Rank 0 is the class attribute
  REQUIRE(ranks[0] == 0);
  REQUIRE(indices[0] == 0);

  // All non-class attributes should appear exactly once
  REQUIRE(ranks.size() == 3);

  // No duplicate ranks
  std::vector<std::size_t> sorted_ranks(ranks.begin(), ranks.end());
  std::sort(sorted_ranks.begin(), sorted_ranks.end());
  for (std::size_t i = 1; i < sorted_ranks.size(); ++i) {
    REQUIRE(sorted_ranks[i] != sorted_ranks[i - 1]);
  }
}

TEST_CASE("mrmr callback receives all ranks", "[mrmr]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream ss(str);
  dataset<unsigned char> ds(ss, dataset<unsigned char>::ROUND);

  std::size_t callback_count = 0;
  auto result = mrmr(ds, 0,
                     [&callback_count](std::size_t, std::size_t, std::string const &, double,
                                       double, double) { ++callback_count; });

  REQUIRE(callback_count == ds.num_attributes());
  REQUIRE(std::get<0>(result).size() == ds.num_attributes());
}

TEST_CASE("triangular_mi_cache symmetry", "[mrmr]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream ss(str);
  dataset<unsigned char> ds(ss, dataset<unsigned char>::ROUND);

  std::vector<std::size_t> indices = {0, 1, 2};
  triangular_mi_cache<dataset<unsigned char>> cache(ds, indices);

  // MI(a,b) == MI(b,a)
  REQUIRE(cache.get(0, 1) == cache.get(1, 0));
  REQUIRE(cache.get(0, 2) == cache.get(2, 0));
  REQUIRE(cache.get(1, 2) == cache.get(2, 1));

  // MI(a,a) == 0
  REQUIRE(cache.get(0, 0) == 0.0);
  REQUIRE(cache.get(1, 1) == 0.0);

  // Cache matches on-the-fly computation
  REQUIRE(cache.get(0, 1) == ds.mutual_information(0, 1));
  REQUIRE(cache.get(0, 2) == ds.mutual_information(0, 2));
  REQUIRE(cache.get(1, 2) == ds.mutual_information(1, 2));
}

// ============================================================================
// mRMRe ensemble tests
// ============================================================================

TEST_CASE("mrmre exhaustive produces multiple solutions", "[mrmre]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream ss(str);
  dataset<unsigned char> ds(ss, dataset<unsigned char>::ROUND);

  auto result = mrmre(ds, 0, 2, 2, mrmre_method::EXHAUSTIVE);

  // Should produce 2 solutions (2 useful attributes = 2 possible seeds)
  REQUIRE(result.solutions.size() == 2);

  // Each solution should have 2 selected features
  REQUIRE(result.solutions[0].selected_indices.size() == 2);
  REQUIRE(result.solutions[1].selected_indices.size() == 2);

  // The two solutions should start with different features
  REQUIRE(result.solutions[0].selected_indices[0] != result.solutions[1].selected_indices[0]);

  // Consensus ranking should cover all non-class attributes
  REQUIRE(result.consensus_ranking.size() == ds.num_attributes());
  REQUIRE(result.feature_frequencies.size() == ds.num_attributes());
}

TEST_CASE("mrmre exhaustive consensus ranks frequent features first", "[mrmre]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream ss(str);
  dataset<unsigned char> ds(ss, dataset<unsigned char>::ROUND);

  auto result = mrmre(ds, 0, 2, 2, mrmre_method::EXHAUSTIVE);

  // Both solutions select both useful features (just in different order)
  // so both attr1 and attr2 should have frequency 2
  // The class attribute (index 0) should have frequency 0
  REQUIRE(result.feature_frequencies[0] == 0);

  // The top consensus features should be the useful ones (frequency > 0)
  REQUIRE(result.feature_frequencies[result.consensus_ranking[0]] > 0);
}

TEST_CASE("mrmr on bootstrap view produces valid ranking", "[mrmre]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream ss(str);
  dataset<unsigned char> ds(ss, dataset<unsigned char>::ROUND);

  std::mt19937 gen(42);
  auto view = dataset_view<unsigned char>::bootstrap(ds, gen);

  // Run mRMR on the view — should produce a valid ranking
  auto result = mrmr(view, 0);
  auto &ranks = std::get<0>(result);
  auto &indices = std::get<1>(result);

  REQUIRE(ranks.size() == view.num_attributes());
  REQUIRE(ranks[0] == 0);   // rank 0 is class
  REQUIRE(indices[0] == 0); // class attribute index
}

// ============================================================================
// Missing value tests
// ============================================================================

TEST_CASE("missing sentinel value", "[missing]") {
  REQUIRE(missing_sentinel<unsigned char>::value == 255);
  REQUIRE(is_missing<unsigned char>(255));
  REQUIRE(!is_missing<unsigned char>(0));
  REQUIRE(!is_missing<unsigned char>(254));
}

TEST_CASE("impute_mode replaces missing with most frequent value", "[missing]") {
  // 3 attributes, 5 instances, column-major
  // attr 0: [0, 1, 0, MISSING, 0]  → mode = 0
  // attr 1: [1, 1, 2, 2, MISSING]  → mode = 1 or 2 (first found)
  // attr 2: [3, 3, 3, 3, 3]        → no missing
  std::vector<unsigned char> data = {0, 1, 0, 255, 0, 1, 1, 2, 2, 255, 3, 3, 3, 3, 3};
  impute_mode(data.data(), 3, 5);

  REQUIRE(data[3] == 0);   // attr 0, inst 3: was missing → mode = 0
  REQUIRE(data[9] != 255); // attr 1, inst 4: was missing → imputed
  // attr 2 unchanged
  REQUIRE(data[10] == 3);
  REQUIRE(data[14] == 3);
}

TEST_CASE("impute_median replaces missing with lower median", "[missing]") {
  // attr 0: [0, 2, 4, MISSING, 6] → sorted non-missing = [0, 2, 4, 6]
  // Lower median for even count: index (4-1)/2 = 1 → value 2
  std::vector<unsigned char> data = {0, 2, 4, 255, 6};
  impute_median(data.data(), 1, 5);
  REQUIRE(data[3] == 2); // lower median of [0, 2, 4, 6]
}

TEST_CASE("impute_mean replaces missing with rounded mean", "[missing]") {
  // attr 0: [1, 2, 3, MISSING] → mean = 2.0, rounded = 2
  std::vector<unsigned char> data = {1, 2, 3, 255};
  impute_mean(data.data(), 1, 4);
  REQUIRE(data[3] == 2);
}

TEST_CASE("count_missing identifies missing values per attribute", "[missing]") {
  // attr 0: no missing, attr 1: 2 missing
  std::vector<unsigned char> data = {0, 1, 2, 3, 255, 1, 255, 3};
  auto counts = count_missing(data.data(), 2, 4);
  REQUIRE(counts[0] == 0);
  REQUIRE(counts[1] == 2);
}

#ifdef MRMR_HAS_CONTINUOUS

// ============================================================================
// Continuous dataset / KSG tests
// ============================================================================

TEST_CASE("continuous_dataset construction and basic properties", "[continuous]") {
  // 3 attributes, 100 instances
  std::vector<double> data(300);
  std::mt19937 gen(42);
  std::normal_distribution<double> dist(0.0, 1.0);
  for (auto &v : data) {
    v = dist(gen);
  }

  continuous_dataset<double> ds(data, 100, 3, {"x", "y", "z"});
  REQUIRE(ds.num_instances() == 100);
  REQUIRE(ds.num_attributes() == 3);
  REQUIRE(ds.attribute_name(0) == "x");
  REQUIRE(ds.attribute_entropy(0) > 0); // has variation
}

TEST_CASE("KSG MI on correlated Gaussian is approximately correct", "[continuous]") {
  // Generate bivariate Gaussian with known correlation rho=0.8
  // Analytical MI = -0.5 * log2(1 - rho^2)
  double rho = 0.8;
  double true_mi_bits = -0.5 * std::log2(1.0 - rho * rho);

  std::size_t n = 5000;
  std::mt19937 gen(42);
  std::normal_distribution<double> norm(0.0, 1.0);

  // class attribute (binary for mRMR compatibility), x, y
  std::vector<double> data(n * 3);
  for (std::size_t i = 0; i < n; ++i) {
    double z1 = norm(gen);
    double z2 = norm(gen);
    double x = z1;
    double y = rho * z1 + std::sqrt(1.0 - rho * rho) * z2;
    data[i * 3 + 0] = (i % 2 == 0) ? 0.0 : 1.0; // class
    data[i * 3 + 1] = x;
    data[i * 3 + 2] = y;
  }

  continuous_dataset<double> ds(data, n, 3, {"class", "x", "y"}, 6);

  double mi = ds.mutual_information(1, 2);
  // KSG estimate should be in the right ballpark for N=5000.
  // KSG can have significant variance at moderate N; use wide tolerance.
  REQUIRE(mi > true_mi_bits * 0.5);
  REQUIRE(mi < true_mi_bits * 2.0);
}

TEST_CASE("mrmr works with continuous_dataset", "[continuous]") {
  std::size_t n = 200;
  std::mt19937 gen(42);
  std::normal_distribution<double> norm(0.0, 1.0);

  // class (0/1), feature correlated with class, random feature
  std::vector<double> data(n * 3);
  for (std::size_t i = 0; i < n; ++i) {
    double cls = (i < n / 2) ? 0.0 : 1.0;
    double corr_feat = cls + norm(gen) * 0.5; // correlated with class
    double rand_feat = norm(gen);             // uncorrelated
    data[i * 3 + 0] = cls;
    data[i * 3 + 1] = corr_feat;
    data[i * 3 + 2] = rand_feat;
  }

  continuous_dataset<double> ds(data, n, 3, {"class", "correlated", "random"}, 6);
  auto result = mrmr(ds, 0);

  auto &ranks = std::get<0>(result);
  auto &indices = std::get<1>(result);

  REQUIRE(ranks.size() == 3);
  REQUIRE(indices[0] == 0); // class at rank 0
  // Correlated feature should be ranked before random feature
  REQUIRE(indices[1] == 1);
}

TEST_CASE("mixed_dataset parses name:type header", "[mixed]") {
  std::string str("class:discrete\tfeature:continuous\tcat:discrete\n"
                  "0\t1.5\t2\n0\t2.3\t1\n1\t0.8\t3\n1\t3.1\t2\n");
  std::stringstream ss(str);
  mixed_dataset ds(ss);

  REQUIRE(ds.num_instances() == 4);
  REQUIRE(ds.num_attributes() == 3);
  REQUIRE(ds.attribute_name(0) == "class");
  REQUIRE(ds.attribute_name(1) == "feature");
  REQUIRE(ds.type_of(0) == column_type::DISCRETE);
  REQUIRE(ds.type_of(1) == column_type::CONTINUOUS);
  REQUIRE(ds.type_of(2) == column_type::DISCRETE);
}

TEST_CASE("mixed_dataset bare names default to discrete", "[mixed]") {
  std::string str("a\tb\tc:continuous\n0\t1\t1.5\n1\t0\t2.3\n");
  std::stringstream ss(str);
  mixed_dataset ds(ss);

  REQUIRE(ds.type_of(0) == column_type::DISCRETE);
  REQUIRE(ds.type_of(1) == column_type::DISCRETE);
  REQUIRE(ds.type_of(2) == column_type::CONTINUOUS);
}

TEST_CASE("mixed_dataset MI dispatch: DD, CC, DC pairs", "[mixed]") {
  // 4 attrs: class(D), discrete(D), continuous1(C), continuous2(C)
  std::string str("class:discrete\td:discrete\tc1:continuous\tc2:continuous\n"
                  "0\t0\t1.0\t2.0\n0\t1\t1.5\t2.5\n0\t0\t0.5\t1.5\n"
                  "1\t1\t3.0\t4.0\n1\t0\t3.5\t4.5\n1\t1\t2.5\t3.5\n");
  std::stringstream ss(str);
  mixed_dataset ds(ss);

  // DD pair
  double dd_mi = ds.mutual_information(0, 1);
  REQUIRE(dd_mi >= 0.0);

  // CC pair
  double cc_mi = ds.mutual_information(2, 3);
  REQUIRE(cc_mi >= 0.0);

  // DC pair (Ross estimator)
  double dc_mi = ds.mutual_information(0, 2);
  REQUIRE(dc_mi >= 0.0);
}

TEST_CASE("mrmr works with mixed_dataset", "[mixed]") {
  std::string str("class:discrete\td:discrete\tc:continuous\n"
                  "0\t0\t1.0\n0\t1\t1.5\n0\t0\t0.5\n"
                  "1\t1\t3.0\n1\t0\t3.5\n1\t1\t2.5\n");
  std::stringstream ss(str);
  mixed_dataset ds(ss);

  auto result = mrmr(ds, 0);
  auto &ranks = std::get<0>(result);
  REQUIRE(ranks.size() == 3);
  REQUIRE(ranks[0] == 0); // class at rank 0
}

#endif // MRMR_HAS_CONTINUOUS
