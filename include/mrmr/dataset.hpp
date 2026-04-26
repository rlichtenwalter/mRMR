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

#ifndef MRMR_DATASET_HPP
#define MRMR_DATASET_HPP

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <mrmr/attribute_information.hpp>
#include <mrmr/detail/delimiter_ctype.hpp>
#include <mrmr/matrix.hpp>
#include <mrmr/mi_policy.hpp>
#include <mrmr/typedef.hpp>
#include <stdexcept>
#include <valarray>
#include <vector>

/**
 * @brief Dataset container for discretized tabular data with cached information-theoretic measures.
 *
 * Holds a collection of named attributes over a set of instances. On construction,
 * floating-point or integer input values are discretized and compacted to contiguous
 * unsigned integer indices in [0, k) per attribute, enabling efficient histogram-based
 * mutual information computation. Entropy and marginal probabilities are precomputed
 * and cached for all attributes.
 *
 * Input data must be complete (no missing values). This class is not thread-safe
 * for concurrent mutual_information() calls on the same instance.
 *
 * @tparam T Unsigned integer storage type. Must have
 *           std::numeric_limits<T>::max() <= 255 (typically unsigned char).
 */
template <typename T> class dataset {
  static_assert(std::numeric_limits<T>::max() <= 255,
                "dataset only supports storage types with max value <= 255");

  template <typename U> friend std::ostream &operator<<(std::ostream &os, dataset<U> const &m);
  template <typename U> friend class dataset_view;

private:
  using itype = long;
  using fptype = double;

public:
  /** @brief Alias for the element storage type. */
  using value_type = T;

  /**
   * @brief Method used to map continuous values to integers during discretization.
   *
   * - ROUND:    std::round() — nearest integer, ties away from zero.
   * - FLOOR:    std::floor() — largest integer not greater than the value.
   * - CEILING:  std::ceil()  — smallest integer not less than the value.
   * - TRUNCATE: std::trunc() — integer part with the fractional part discarded.
   */
  enum discretization_method : char { ROUND = 0, FLOOR = 1, CEILING = 2, TRUNCATE = 3 };

  /** @brief Construct an empty dataset with no instances or attributes. */
  dataset();

  /**
   * @brief Construct a dataset by reading a delimited text stream.
   *
   * Expects a header line containing tab-separated (or @p delimiter-separated)
   * attribute names followed by rows of numeric data, one instance per row.
   * Values are discretized according to @p dm and compacted to dense indices.
   *
   * @param is        Input stream positioned at the beginning of the header line.
   * @param dm        Discretization method applied to each value.
   * @param delimiter Field separator character (default tab).
   * @throws std::runtime_error If the header newline is missing or column counts
   *                            are inconsistent across rows.
   */
  dataset(std::istream &, discretization_method dm = ROUND, char delimiter = '\t',
          missing_strategy ms = missing_strategy::ERROR);

  /**
   * @brief Construct a dataset from an in-memory data vector.
   *
   * The vector must contain exactly @p num_instances * @p num_attributes elements
   * laid out in row-major order (instance-major) unless @p column_major is true,
   * in which case column-major (attribute-major) layout is assumed.
   *
   * @tparam U Source element type; must be convertible to the storage type T.
   * @param data           Flat vector of data values.
   * @param num_instances  Number of instances (rows).
   * @param num_attributes Number of attributes (columns).
   * @param column_major   If true, @p data is in column-major order.
   * @param names          Attribute names; if empty, names are generated as "attr0", "attr1", ...
   * @param dm             Discretization method applied to each value.
   * @param delimiter      Field separator character used for stream output.
   * @throws std::logic_error If data.size() != num_instances * num_attributes, or
   *                          if names is non-empty and names.size() != num_attributes.
   */
  template <typename U>
  dataset(std::vector<U> data, std::size_t num_instances, std::size_t num_attributes,
          bool column_major = false, std::vector<std::string> names = std::vector<std::string>(),
          discretization_method dm = ROUND, char delimiter = '\t');

  /** @brief Return the number of instances (rows) in the dataset. */
  std::size_t num_instances() const;

  /** @brief Return the number of attributes (columns) in the dataset. */
  std::size_t num_attributes() const;

  /**
   * @brief Return the name of the attribute at the given index.
   *
   * @param attribute_num Attribute index in [0, num_attributes()).
   * @return Attribute name string.
   */
  std::string attribute_name(std::size_t attribute_num) const;

  /**
   * @brief Return the Shannon entropy (in bits) of the given attribute.
   *
   * @param attribute_num Attribute index in [0, num_attributes()).
   * @return Entropy value >= 0.
   */
  double attribute_entropy(std::size_t attribute_num) const;

  /**
   * @brief Compute the mutual information between two attributes.
   *
   * Uses precomputed marginal probabilities and a thread-local scratch
   * buffer to build the joint histogram. Returns 0 if either attribute has
   * only one distinct value. Thread-safe for concurrent calls on the same instance.
   *
   * @param attribute1 Index of the first attribute in [0, num_attributes()).
   * @param attribute2 Index of the second attribute in [0, num_attributes()).
   * @return Mutual information I(attribute1; attribute2) >= 0, in bits.
   */
  double mutual_information(std::size_t attribute1, std::size_t attribute2) const;

  /**
   * @brief Access a single discretized cell value.
   *
   * @param attribute Attribute index in [0, num_attributes()).
   * @param instance  Instance index in [0, num_instances()).
   * @return The discretized, compacted value at the given position.
   */
  T operator()(std::size_t attribute, std::size_t instance) const;

private:
  template <typename U>
  void transpose_and_discretize(matrix<U> const &temp, discretization_method dm,
                                missing_strategy ms);
  void compute_attribute_information();
  std::vector<std::string> _names;
  std::vector<attribute_information<T>> _attr_info;
  matrix<T> _data;
  char _delimiter;
  bool _use_pairwise_mi;
};

template <typename T>
template <typename U>
void dataset<T>::transpose_and_discretize(matrix<U> const &temp, discretization_method dm,
                                          missing_strategy ms) {
  // Discretize, transpose to column-major storage, translate to non-negative values,
  // and compact to contiguous unsigned integer indices for efficient histogram computation.

  // Tag-dispatch helper for finiteness checks on floating-point types.
  struct finite_check {
    static bool is_nan(U, std::false_type) { return false; } // integer: never NaN
    static bool is_nan(U value, std::true_type) {
      return !std::isfinite(static_cast<double>(value));
    }
  };

  // Sentinel value for missing data
  constexpr auto missing_itype_sentinel = static_cast<itype>(missing_sentinel<T>::value);

  auto discretize_value = [dm, ms](U value) -> itype {
    // Check for non-finite values (NaN/Inf)
    if (finite_check::is_nan(value, std::is_floating_point<U>{})) {
      if (ms == missing_strategy::ERROR) {
        throw std::runtime_error("non-finite value (NaN or Inf) encountered during discretization");
      }
      // Map NaN/Inf to missing sentinel for imputation or pairwise handling
      return missing_itype_sentinel;
    }

    double rounded;
    switch (dm) {
    case ROUND:
      rounded = std::round(static_cast<double>(value));
      break;
    case FLOOR:
      rounded = std::floor(static_cast<double>(value));
      break;
    case CEILING:
      rounded = std::ceil(static_cast<double>(value));
      break;
    case TRUNCATE:
    default:
      rounded = std::trunc(static_cast<double>(value));
      break;
    }

    // Guard against overflow when converting to long.
    // Note: static_cast<double>(LONG_MAX) rounds UP to 2^63 (not exactly representable),
    // so >= is required to reject values at or above 2^63 which cannot be stored in long.
    if (rounded >= static_cast<double>(std::numeric_limits<itype>::max()) ||
        rounded < static_cast<double>(std::numeric_limits<itype>::min())) {
      throw std::runtime_error("discretized value " + std::to_string(rounded) +
                               " exceeds representable integer range");
    }
    return static_cast<itype>(rounded);
  };

  std::size_t n_inst = temp.num_rows();
  std::size_t n_attr = num_attributes();

  // Pass 1: Discretize all values into long intermediates, track min/max per attribute.
  // Column-major layout (attr * n_inst + inst) for efficient per-attribute processing in pass 2.
  std::vector<itype> discretized(n_attr * n_inst);
  std::vector<itype> minima(n_attr, std::numeric_limits<itype>::max());
  std::vector<itype> maxima(n_attr, std::numeric_limits<itype>::min());

  for (std::size_t inst = 0; inst < n_inst; ++inst) {
    for (std::size_t attr = 0; attr < n_attr; ++attr) {
      itype val = discretize_value(temp(inst, attr));
      discretized[attr * n_inst + inst] = val;
      // Skip sentinel values in min/max tracking
      if (val != missing_itype_sentinel) {
        if (val < minima[attr]) {
          minima[attr] = val;
        }
        if (val > maxima[attr]) {
          maxima[attr] = val;
        }
      }
    }
  }

  // Compute ranges using unsigned arithmetic to avoid signed overflow.
  // The range maxima[attr] - minima[attr] could exceed LONG_MAX if values span
  // a wide signed range (e.g., large positive and large negative values).
  using utype = unsigned long;
  std::vector<utype> ranges(n_attr, 0);
  for (std::size_t attr = 0; attr < n_attr; ++attr) {
    if (minima[attr] > maxima[attr]) {
      continue; // empty attribute (no instances)
    }
    // Safe unsigned subtraction: maxima >= minima is guaranteed here
    ranges[attr] = static_cast<utype>(maxima[attr]) - static_cast<utype>(minima[attr]);
    if (ranges[attr] > static_cast<utype>(std::numeric_limits<T>::max())) {
      throw std::runtime_error("attribute '" + attribute_name(attr) + "' range (" +
                               std::to_string(ranges[attr]) + ") exceeds " +
                               std::to_string(static_cast<int>(std::numeric_limits<T>::max())) +
                               " under current discretization");
    }
  }

  // Pass 2: Translate to [0, range], then compact non-contiguous values to dense
  // contiguous indices 0..k-1. Store results in column-major _data matrix.
  _data = matrix<T>(n_attr, n_inst);

  for (std::size_t attr = 0; attr < n_attr; ++attr) {
    // ranges[attr] is at most T::max() (255), so histogram is at most 256 entries
    std::size_t range_size = static_cast<std::size_t>(ranges[attr]) + 1;

    // Build histogram of translated values for this attribute (skip missing)
    std::vector<unsigned int> histogram(range_size, 0);
    for (std::size_t inst = 0; inst < n_inst; ++inst) {
      itype val = discretized[attr * n_inst + inst];
      if (val != missing_itype_sentinel) {
        auto translated = static_cast<std::size_t>(val - minima[attr]);
        ++histogram[translated];
      }
    }

    // Build rank map: maps each populated translated value to a dense index
    std::vector<T> rank_map(range_size, 0);
    T rank = 0;
    for (std::size_t v = 0; v < range_size; ++v) {
      if (histogram[v] > 0) {
        rank_map[v] = rank++;
      }
    }

    // Store compacted values; map missing to sentinel
    for (std::size_t inst = 0; inst < n_inst; ++inst) {
      itype val = discretized[attr * n_inst + inst];
      if (val == missing_itype_sentinel) {
        _data(attr, inst) = missing_sentinel<T>::value;
      } else {
        auto translated = static_cast<std::size_t>(val - minima[attr]);
        _data(attr, inst) = rank_map[translated];
      }
    }
  }
}

template <typename T> void dataset<T>::compute_attribute_information() {
  // perform basic attribute computations and cache results
  _attr_info.reserve(num_attributes());
  for (std::size_t attribute_num = 0; attribute_num < num_attributes(); ++attribute_num) {
    auto attribute_begin = &_data(attribute_num, 0);
    auto attribute_end = attribute_begin + num_instances();
    _attr_info.emplace_back(attribute_begin, attribute_end);
  }
}

template <typename T>
dataset<T>::dataset() : _data(0, 0), _delimiter('\t'), _use_pairwise_mi(false) {}

template <typename T>
dataset<T>::dataset(std::istream &is, discretization_method dm, char delimiter, missing_strategy ms)
    : _delimiter(delimiter), _use_pairwise_mi(ms == missing_strategy::PAIRWISE) {
  // the pointer below is managed via the library interface
  is.imbue(std::locale(is.getloc(), new delimiter_ctype(_delimiter)));

  // read header line with attribute names
  std::string name;
  while (is.good() && is.peek() != '\n') {
    is >> name;
    _names.push_back(name);
  }
  if (is.peek() != '\n') {
    throw std::runtime_error("missing required newline after header");
  }

  // read data matrix — always allow missing token parsing so the full file is
  // read even when the strategy is ERROR (error is reported during discretization)
  matrix<fptype> temp;
  temp.set_delimiter(_delimiter);
  temp.set_allow_missing(true);
  is >> temp;

  transpose_and_discretize(temp, dm, ms);

  // Apply imputation if requested (operates on compacted column-major _data)
  if (ms == missing_strategy::IMPUTE_MODE) {
    impute_mode(&_data(0, 0), num_attributes(), num_instances());
  } else if (ms == missing_strategy::IMPUTE_MEDIAN) {
    impute_median(&_data(0, 0), num_attributes(), num_instances());
  } else if (ms == missing_strategy::IMPUTE_MEAN) {
    impute_mean(&_data(0, 0), num_attributes(), num_instances());
  } else if (ms == missing_strategy::ERROR) {
    validate_no_missing(&_data(0, 0), num_attributes(), num_instances(), _names);
  }
  // PAIRWISE: no imputation; sentinel values remain for MI to handle

  compute_attribute_information();
}

template <typename T>
template <typename U>
dataset<T>::dataset(std::vector<U> data, std::size_t num_instances, std::size_t num_attributes,
                    bool column_major, std::vector<std::string> names, discretization_method dm,
                    char delimiter)
    : _names(std::move(names)), _delimiter(delimiter) {
  if (num_instances * num_attributes != data.size()) {
    throw std::logic_error("data size must equal the product of num_instances and num_attributes");
  }
  if (_names.empty()) {
    for (std::size_t i = 0; i < num_attributes; ++i) {
      _names.emplace_back("attr" + std::to_string(i));
    }
  } else if (num_attributes != _names.size()) {
    throw std::logic_error("names size must either equal num_attributes or be 0");
  }
  matrix<T> temp(num_instances, num_attributes);
  for (std::size_t instance_num = 0; instance_num < num_instances; ++instance_num) {
    for (std::size_t attribute_num = 0; attribute_num < num_attributes; ++attribute_num) {
      if (column_major) {
        temp(instance_num, attribute_num) = data[attribute_num * num_instances + instance_num];
      } else {
        temp(instance_num, attribute_num) = data[instance_num * num_attributes + attribute_num];
      }
    }
  }

  transpose_and_discretize(temp, dm, missing_strategy::ERROR);

  compute_attribute_information();
}

template <typename T> std::size_t dataset<T>::num_instances() const { return _data.num_columns(); }

template <typename T> std::size_t dataset<T>::num_attributes() const { return _names.size(); }

template <typename T> std::string dataset<T>::attribute_name(std::size_t attribute_num) const {
  return _names[attribute_num];
}

template <typename T> double dataset<T>::attribute_entropy(std::size_t attribute_num) const {
  return _attr_info[attribute_num].entropy();
}

template <typename T> T dataset<T>::operator()(std::size_t attribute, std::size_t instance) const {
  return _data(attribute, instance);
}

template <typename T>
double dataset<T>::mutual_information(std::size_t attribute1, std::size_t attribute2) const {
  if (_use_pairwise_mi) {
    // Pairwise-complete: skip instances where either attribute has sentinel value.
    // Column data pointers for the policy to check missingness.
    T const *col1 = &_data(attribute1, 0);
    T const *col2 = &_data(attribute2, 0);
    return compute_mi(*this, _attr_info.at(attribute1), _attr_info.at(attribute2), attribute1,
                      attribute2, pairwise_complete_policy<T>{col1, col2});
  }
  return compute_mi(*this, _attr_info.at(attribute1), _attr_info.at(attribute2), attribute1,
                    attribute2, unweighted_policy{});
}

/**
 * @brief Write a dataset to an output stream.
 *
 * Outputs a header line of delimiter-separated attribute names followed by rows
 * of data, one instance per row. Element values are written as unsigned integers.
 *
 * @tparam U Element storage type.
 * @param os   Output stream.
 * @param data Dataset to write.
 * @return Reference to @p os.
 */
template <typename T> std::ostream &operator<<(std::ostream &os, dataset<T> const &data) {
  if (data.num_attributes() > 0) {
    os << data._names.at(0);
    for (std::size_t i = 1; i < data.num_attributes(); ++i) {
      os << data._delimiter << data._names.at(i);
    }
    os << '\n';
    matrix<T> transposed = data._data.transpose();
    transposed.set_delimiter(data._delimiter);
    transposed.write_to(os);
  }
  return os;
}

#endif
