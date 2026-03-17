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

char DELIMITER = '\t';

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
