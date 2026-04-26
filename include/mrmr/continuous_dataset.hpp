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

#ifndef MRMR_CONTINUOUS_DATASET_HPP
#define MRMR_CONTINUOUS_DATASET_HPP

#ifdef MRMR_HAS_CONTINUOUS

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <mrmr/detail/delimiter_ctype.hpp>
#include <mrmr/ksg_estimator.hpp>
#include <mrmr/matrix.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

/**
 * @brief Dataset container for continuous (real-valued) data with KSG-based MI.
 *
 * Stores raw floating-point values without discretization. Mutual information
 * is computed using the KSG Algorithm 1 (Kraskov et al., 2004) with Chebyshev
 * distance, providing consistent MI estimates without discretization bias.
 *
 * Satisfies the DataSource concept: num_instances(), num_attributes(),
 * attribute_name(), attribute_entropy(), mutual_information().
 *
 * @tparam FloatT Floating-point storage type (typically double; float halves
 *                cache pressure at the cost of precision for distance computation).
 */
template <typename FloatT = double> class continuous_dataset {
  static_assert(std::is_floating_point<FloatT>::value,
                "continuous_dataset requires a floating-point storage type");

public:
  using value_type = FloatT;

  /** @brief Construct an empty dataset. */
  continuous_dataset() : _num_instances(0) {}

  /**
   * @brief Construct from a delimited text stream.
   *
   * @param is        Input stream with header + data rows.
   * @param delimiter Field separator (default tab).
   * @param ksg_k     Number of neighbors for KSG MI estimation (default 6).
   */
  continuous_dataset(std::istream &is, char delimiter = '\t', std::size_t ksg_k = 6);

  /**
   * @brief Construct from an in-memory data vector.
   *
   * @param data           Flat row-major data vector.
   * @param num_instances  Number of instances (rows).
   * @param num_attributes Number of attributes (columns).
   * @param names          Attribute names.
   * @param ksg_k          KSG neighbor count.
   */
  continuous_dataset(std::vector<FloatT> data, std::size_t num_instances,
                     std::size_t num_attributes, std::vector<std::string> names = {},
                     std::size_t ksg_k = 6);

  std::size_t num_instances() const { return _num_instances; }
  std::size_t num_attributes() const { return _names.size(); }
  std::string attribute_name(std::size_t attr) const { return _names[attr]; }
  std::size_t ksg_k() const { return _ksg_k; }

  /**
   * @brief Return whether this attribute has variation (non-constant).
   *
   * For continuous data, differential entropy can be negative, so the
   * discrete convention of "entropy > 0 means useful" does not apply.
   * This method checks whether the attribute has at least 2 distinct values.
   * mrmr() uses this via attribute_entropy() > 0 to filter useful attributes.
   * We return 1.0 for varying attributes and 0.0 for constant, preserving
   * the mrmr() filter semantics.
   */
  double attribute_entropy(std::size_t attr) const { return _has_variation[attr] ? 1.0 : 0.0; }

  /**
   * @brief Compute MI between two continuous attributes using KSG Algorithm 1.
   */
  double mutual_information(std::size_t attr1, std::size_t attr2) const;

  /** @brief Access a single cell value. */
  FloatT operator()(std::size_t attr, std::size_t inst) const {
    return _data[attr * _num_instances + inst];
  }

private:
  void compute_variation();

  // Tag-dispatch helpers for mutual_information: FloatT==double passes column
  // pointers directly (zero-copy); other FloatT converts via thread_local scratch.
  double mutual_information_ksg(std::size_t attr1, std::size_t attr2,
                                std::true_type /*is_double*/) const;
  double mutual_information_ksg(std::size_t attr1, std::size_t attr2,
                                std::false_type /*not_double*/) const;

  std::vector<std::string> _names;
  std::vector<FloatT> _data; // column-major: attr * num_instances + inst
  std::size_t _num_instances;
  std::vector<bool> _has_variation;
  std::size_t _ksg_k;
};

// ============================================================================
// Implementation
// ============================================================================

template <typename FloatT>
continuous_dataset<FloatT>::continuous_dataset(std::istream &is, char delimiter, std::size_t ksg_k)
    : _ksg_k(ksg_k) {
  is.imbue(std::locale(is.getloc(), new delimiter_ctype(delimiter)));

  // Read header
  std::string name;
  while (is.good() && is.peek() != '\n') {
    is >> name;
    _names.push_back(name);
  }
  if (is.peek() != '\n') {
    throw std::runtime_error("missing required newline after header");
  }

  // Read data into row-major buffer, then transpose to column-major
  std::vector<FloatT> row_major;
  std::size_t n_attr = _names.size();
  _num_instances = 0;
  FloatT val;
  while (is >> val) {
    row_major.push_back(val);
    // After each value, check separator
    if (row_major.size() % n_attr != 0) {
      // Still reading within a row
    }
  }
  _num_instances = row_major.size() / n_attr;
  if (_num_instances * n_attr != row_major.size()) {
    throw std::runtime_error("data size is not a multiple of attribute count");
  }

  // Transpose to column-major
  _data.resize(n_attr * _num_instances);
  for (std::size_t inst = 0; inst < _num_instances; ++inst) {
    for (std::size_t attr = 0; attr < n_attr; ++attr) {
      _data[attr * _num_instances + inst] = row_major[inst * n_attr + attr];
    }
  }

  compute_variation();
}

template <typename FloatT>
continuous_dataset<FloatT>::continuous_dataset(std::vector<FloatT> data, std::size_t num_instances,
                                               std::size_t num_attributes,
                                               std::vector<std::string> names, std::size_t ksg_k)
    : _names(std::move(names)), _num_instances(num_instances), _ksg_k(ksg_k) {
  if (num_instances * num_attributes != data.size()) {
    throw std::logic_error("data size must equal num_instances * num_attributes");
  }
  if (_names.empty()) {
    for (std::size_t i = 0; i < num_attributes; ++i) {
      _names.emplace_back("attr" + std::to_string(i));
    }
  }

  // Transpose from row-major to column-major
  _data.resize(num_attributes * num_instances);
  for (std::size_t inst = 0; inst < num_instances; ++inst) {
    for (std::size_t attr = 0; attr < num_attributes; ++attr) {
      _data[attr * num_instances + inst] = data[inst * num_attributes + attr];
    }
  }

  compute_variation();
}

template <typename FloatT> void continuous_dataset<FloatT>::compute_variation() {
  _has_variation.resize(num_attributes(), false);
  if (_num_instances == 0) {
    return;
  }
  for (std::size_t attr = 0; attr < num_attributes(); ++attr) {
    FloatT first = _data[attr * _num_instances];
    for (std::size_t inst = 1; inst < _num_instances; ++inst) {
      if (_data[attr * _num_instances + inst] != first) {
        _has_variation[attr] = true;
        break;
      }
    }
  }
}

template <typename FloatT>
double continuous_dataset<FloatT>::mutual_information(std::size_t attr1, std::size_t attr2) const {
  if (!_has_variation[attr1] || !_has_variation[attr2]) {
    return 0.0;
  }

  return mutual_information_ksg(attr1, attr2, std::is_same<FloatT, double>{});
}

// Tag-dispatch overload: FloatT is double — pass column pointers directly.
// Zero-copy access enables ksg_mi's internal sort cache to identify and reuse
// previously sorted columns across MI calls (pointer-keyed single-entry cache).
template <typename FloatT>
double continuous_dataset<FloatT>::mutual_information_ksg(std::size_t attr1, std::size_t attr2,
                                                          std::true_type /*is_double*/) const {
  return ksg_mi(&_data[attr1 * _num_instances], &_data[attr2 * _num_instances], _num_instances,
                _ksg_k);
}

// Tag-dispatch overload: FloatT is not double — convert and cache via thread_local.
//
// ksg_mi's internal pointer-keyed cache cannot be used here: it keys on the
// double* output buffer, which is a shared scratch buffer (same pointer,
// different content each call → false hits). Instead, we maintain our own
// single-entry cache keyed on the SOURCE float pointer (&_data[attr * N]),
// which is unique and stable per column. This gives the same T=1 outer-loop
// caching benefit as the double path: when attr1 is fixed across inner-loop
// iterations, both the float→double conversion and the sort are skipped.
// Pre-sorted arrays are always passed to ksg_mi via x_sorted/y_sorted,
// bypassing its internal cache entirely (get_or_sort returns immediately
// when provided != nullptr, without touching the cache state).
template <typename FloatT>
double continuous_dataset<FloatT>::mutual_information_ksg(std::size_t attr1, std::size_t attr2,
                                                          std::false_type /*not_double*/) const {
  struct col_cache {
    void const *src_key = nullptr;
    std::size_t n = 0;
    std::vector<double> *data = new std::vector<double>();
    std::vector<double> *sorted = new std::vector<double>();
  };
  static thread_local col_cache c1, c2;

  auto convert_and_sort = [this](col_cache &c, std::size_t attr) {
    void const *src = &_data[attr * _num_instances];
    if (c.src_key == src && c.n == _num_instances) {
      return;
    }
    c.data->resize(_num_instances);
    for (std::size_t i = 0; i < _num_instances; ++i) {
      (*c.data)[i] = static_cast<double>(_data[attr * _num_instances + i]);
    }
    *c.sorted = *c.data;
    std::sort(c.sorted->begin(), c.sorted->end());
    c.src_key = src;
    c.n = _num_instances;
  };

  convert_and_sort(c1, attr1);
  convert_and_sort(c2, attr2);
  return ksg_mi(c1.data->data(), c2.data->data(), _num_instances, _ksg_k, c1.sorted->data(),
                c2.sorted->data());
}

#endif // MRMR_HAS_CONTINUOUS

#endif // MRMR_CONTINUOUS_DATASET_HPP
