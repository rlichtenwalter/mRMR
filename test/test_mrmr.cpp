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
#include <mrmr/matrix.hpp>
#include <mrmr/mrmr.hpp>
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
  triangular_mi_cache<unsigned char> cache(ds, indices);

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

TEST_CASE("mrmr_with_seed forces first feature", "[mrmre]") {
  std::string str("class\tattr1\tattr2\n0\t0\t1\n0\t1\t1\n0\t0\t0\n1\t1\t1\n1\t0\t1\n1\t1\t1\n");
  std::stringstream ss(str);
  dataset<unsigned char> ds(ss, dataset<unsigned char>::ROUND);

  auto sol1 = mrmr_with_seed(ds, 0, 1, 2);
  auto sol2 = mrmr_with_seed(ds, 0, 2, 2);

  REQUIRE(sol1.selected_indices[0] == 1);
  REQUIRE(sol2.selected_indices[0] == 2);
  REQUIRE(sol1.selected_indices.size() == 2);
  REQUIRE(sol2.selected_indices.size() == 2);
}
