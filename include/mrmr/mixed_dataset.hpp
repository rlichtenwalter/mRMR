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

#ifndef MRMR_MIXED_DATASET_HPP
#define MRMR_MIXED_DATASET_HPP

#ifdef MRMR_HAS_CONTINUOUS

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <locale>
#include <mrmr/attribute_information.hpp>
#include <mrmr/delimiter_ctype.hpp>
#include <mrmr/ksg_estimator.hpp>
#include <mrmr/mi_policy.hpp>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief Column type for mixed datasets.
 */
enum class column_type : char {
  DISCRETE,  ///< Discretized unsigned char values (histogram MI).
  CONTINUOUS ///< Raw floating-point values (KSG MI).
};

/**
 * @brief Dataset with mixed discrete and continuous columns.
 *
 * Stores discrete columns as unsigned char (same representation as dataset<T>)
 * and continuous columns as double, in type-segregated storage. MI computation
 * dispatches per pair based on the column types:
 * - Discrete × discrete: histogram MI (same as dataset<unsigned char>)
 * - Continuous × continuous: KSG Algorithm 1
 * - Discrete × continuous: Ross (2014) estimator
 *
 * Satisfies the DataSource concept for mrmr().
 *
 * Column types are specified via the `name:type` header convention:
 * - `age:continuous` — continuous column
 * - `gender:discrete` — discrete column
 * - `gender` (no annotation) — defaults to discrete
 */
class mixed_dataset {
public:
  using value_type = double; // Unified external type (internal storage is typed)

  /** @brief Construct an empty dataset. */
  mixed_dataset() : _num_instances(0), _ksg_k(6) {}

  /**
   * @brief Construct from a delimited text stream with name:type header.
   *
   * Parses the header for `name:type` annotations. Bare names (no colon)
   * default to discrete. Only `discrete` and `continuous` are recognized
   * after the final colon; anything else is treated as part of the name.
   *
   * Discrete columns are discretized via TRUNCATE and compacted to [0, k).
   * Continuous columns are stored as raw doubles.
   *
   * @param is        Input stream.
   * @param delimiter Field separator (default tab).
   * @param ksg_k     KSG neighbor count for continuous MI (default 6).
   */
  mixed_dataset(std::istream &is, char delimiter = '\t', std::size_t ksg_k = 6);

  /**
   * @brief Construct from explicit typed columns.
   *
   * @param col_types  Type of each column.
   * @param data       Row-major double data (all values, discrete ones will be cast).
   * @param num_inst   Number of instances.
   * @param num_attr   Number of attributes.
   * @param names      Attribute names.
   * @param ksg_k      KSG neighbor count.
   */
  mixed_dataset(std::vector<column_type> col_types, std::vector<double> data, std::size_t num_inst,
                std::size_t num_attr, std::vector<std::string> names = {}, std::size_t ksg_k = 6);

  std::size_t num_instances() const { return _num_instances; }
  std::size_t num_attributes() const { return _names.size(); }
  std::string attribute_name(std::size_t attr) const { return _names[attr]; }
  column_type type_of(std::size_t attr) const { return _col_types[attr]; }

  /**
   * @brief Return attribute entropy proxy for mrmr() filtering.
   *
   * Returns cached discrete entropy for discrete columns, or 1.0/0.0
   * variation flag for continuous columns (differential entropy can be
   * negative, but mrmr() uses entropy > 0 to filter useful attributes).
   */
  double attribute_entropy(std::size_t attr) const;

  /**
   * @brief Compute MI between two attributes, dispatching by type pair.
   *
   * DD → histogram MI, CC → KSG, DC/CD → Ross (2014).
   */
  double mutual_information(std::size_t attr1, std::size_t attr2) const;

private:
  void parse_header(std::istream &is);
  void build_storage(std::vector<double> const &row_major);
  void compute_statistics();

  std::vector<std::string> _names;
  std::vector<column_type> _col_types;
  std::size_t _num_instances;
  std::size_t _ksg_k;

  // Discrete storage: column-major unsigned char, one column per discrete attribute
  // _discrete_col_index[global_attr] = index into _discrete_cols, or SIZE_MAX if continuous
  std::vector<std::size_t> _discrete_col_index;
  std::vector<std::vector<unsigned char>> _discrete_cols;
  std::vector<attribute_information<unsigned char>> _discrete_info;

  // Continuous storage: column-major double, one column per continuous attribute
  std::vector<std::size_t> _continuous_col_index;
  std::vector<std::vector<double>> _continuous_cols;
  std::vector<bool> _continuous_has_variation;
};

// ============================================================================
// Implementation
// ============================================================================

inline void mixed_dataset::parse_header(std::istream &is) {
  std::string token;
  while (is.good() && is.peek() != '\n') {
    is >> token;

    // Parse name:type — only recognize 'discrete' or 'continuous' after final colon
    auto colon_pos = token.rfind(':');
    if (colon_pos != std::string::npos) {
      std::string type_str = token.substr(colon_pos + 1);
      if (type_str == "discrete") {
        _names.push_back(token.substr(0, colon_pos));
        _col_types.push_back(column_type::DISCRETE);
        continue;
      } else if (type_str == "continuous") {
        _names.push_back(token.substr(0, colon_pos));
        _col_types.push_back(column_type::CONTINUOUS);
        continue;
      }
    }
    // No recognized type annotation — default to discrete
    _names.push_back(token);
    _col_types.push_back(column_type::DISCRETE);
  }
  if (is.peek() != '\n') {
    throw std::runtime_error("missing required newline after header");
  }
}

inline void mixed_dataset::build_storage(std::vector<double> const &row_major) {
  std::size_t n_attr = _names.size();

  _discrete_col_index.assign(n_attr, std::numeric_limits<std::size_t>::max());
  _continuous_col_index.assign(n_attr, std::numeric_limits<std::size_t>::max());

  for (std::size_t attr = 0; attr < n_attr; ++attr) {
    if (_col_types[attr] == column_type::DISCRETE) {
      std::size_t idx = _discrete_cols.size();
      _discrete_col_index[attr] = idx;
      _discrete_cols.emplace_back(_num_instances);

      // Discretize via truncation, translate to [0, max-min], compact
      long minv = std::numeric_limits<long>::max();
      long maxv = std::numeric_limits<long>::min();
      std::vector<long> disc(_num_instances);
      for (std::size_t inst = 0; inst < _num_instances; ++inst) {
        double val = row_major[inst * n_attr + attr];
        if (!std::isfinite(val)) {
          throw std::runtime_error("non-finite value in discrete column '" + _names[attr] + "'");
        }
        disc[inst] = static_cast<long>(std::trunc(val));
        if (disc[inst] < minv)
          minv = disc[inst];
        if (disc[inst] > maxv)
          maxv = disc[inst];
      }

      unsigned long range = static_cast<unsigned long>(maxv) - static_cast<unsigned long>(minv);
      if (range > 255) {
        throw std::runtime_error("discrete column '" + _names[attr] + "' range exceeds 255");
      }

      // Translate and compact
      std::vector<unsigned int> histogram(static_cast<std::size_t>(range) + 1, 0);
      for (std::size_t inst = 0; inst < _num_instances; ++inst) {
        auto translated = static_cast<std::size_t>(disc[inst] - minv);
        ++histogram[translated];
      }
      std::vector<unsigned char> rank_map(static_cast<std::size_t>(range) + 1, 0);
      unsigned char rank = 0;
      for (std::size_t v = 0; v <= static_cast<std::size_t>(range); ++v) {
        if (histogram[v] > 0)
          rank_map[v] = rank++;
      }
      for (std::size_t inst = 0; inst < _num_instances; ++inst) {
        auto translated = static_cast<std::size_t>(disc[inst] - minv);
        _discrete_cols[idx][inst] = rank_map[translated];
      }

    } else {
      std::size_t idx = _continuous_cols.size();
      _continuous_col_index[attr] = idx;
      _continuous_cols.emplace_back(_num_instances);
      for (std::size_t inst = 0; inst < _num_instances; ++inst) {
        _continuous_cols[idx][inst] = row_major[inst * n_attr + attr];
      }
    }
  }
}

inline void mixed_dataset::compute_statistics() {
  // Discrete: build attribute_information
  _discrete_info.clear();
  _discrete_info.reserve(_discrete_cols.size());
  for (auto const &col : _discrete_cols) {
    _discrete_info.emplace_back(col.begin(), col.end());
  }

  // Continuous: check variation
  _continuous_has_variation.clear();
  _continuous_has_variation.resize(_continuous_cols.size(), false);
  for (std::size_t i = 0; i < _continuous_cols.size(); ++i) {
    auto const &col = _continuous_cols[i];
    if (!col.empty()) {
      double first = col[0];
      for (std::size_t j = 1; j < col.size(); ++j) {
        if (col[j] != first) {
          _continuous_has_variation[i] = true;
          break;
        }
      }
    }
  }
}

inline mixed_dataset::mixed_dataset(std::istream &is, char delimiter, std::size_t ksg_k)
    : _ksg_k(ksg_k) {
  is.imbue(std::locale(is.getloc(), new delimiter_ctype(delimiter)));
  parse_header(is);

  // Read all values as doubles
  std::vector<double> row_major;
  double val;
  while (is >> val) {
    row_major.push_back(val);
  }
  _num_instances = row_major.size() / _names.size();
  if (_num_instances * _names.size() != row_major.size()) {
    throw std::runtime_error("data size is not a multiple of attribute count");
  }

  build_storage(row_major);
  compute_statistics();
}

inline mixed_dataset::mixed_dataset(std::vector<column_type> col_types, std::vector<double> data,
                                    std::size_t num_inst, std::size_t num_attr,
                                    std::vector<std::string> names, std::size_t ksg_k)
    : _col_types(std::move(col_types)), _num_instances(num_inst), _ksg_k(ksg_k) {
  if (num_inst * num_attr != data.size()) {
    throw std::logic_error("data size must equal num_instances * num_attributes");
  }
  if (names.empty()) {
    for (std::size_t i = 0; i < num_attr; ++i) {
      _names.emplace_back("attr" + std::to_string(i));
    }
  } else {
    _names = std::move(names);
  }
  if (_col_types.size() != num_attr) {
    throw std::logic_error("col_types size must equal num_attributes");
  }

  build_storage(data);
  compute_statistics();
}

inline double mixed_dataset::attribute_entropy(std::size_t attr) const {
  if (_col_types[attr] == column_type::DISCRETE) {
    return _discrete_info[_discrete_col_index[attr]].entropy();
  } else {
    return _continuous_has_variation[_continuous_col_index[attr]] ? 1.0 : 0.0;
  }
}

inline double mixed_dataset::mutual_information(std::size_t attr1, std::size_t attr2) const {
  auto t1 = _col_types[attr1];
  auto t2 = _col_types[attr2];

  if (t1 == column_type::DISCRETE && t2 == column_type::DISCRETE) {
    // Histogram MI — same as dataset<unsigned char>
    auto const &col1 = _discrete_cols[_discrete_col_index[attr1]];
    auto const &col2 = _discrete_cols[_discrete_col_index[attr2]];
    auto const &info1 = _discrete_info[_discrete_col_index[attr1]];
    auto const &info2 = _discrete_info[_discrete_col_index[attr2]];

    std::size_t k1 = info1.num_values();
    std::size_t k2 = info2.num_values();
    if (k1 == 1 || k2 == 1)
      return 0.0;

    std::vector<std::size_t> scratch(k1 * k2, 0);
    for (std::size_t i = 0; i < _num_instances; ++i) {
      ++scratch[col1[i] * k2 + col2[i]];
    }

    double inv_n = 1.0 / static_cast<double>(_num_instances);
    double mi = 0.0;
    for (std::size_t i = 0; i < k1; ++i) {
      for (std::size_t j = 0; j < k2; ++j) {
        std::size_t count = scratch[i * k2 + j];
        if (count != 0) {
          double jp = count * inv_n;
          double mi_val = info1.marginal_probability(i);
          double mj_val = info2.marginal_probability(j);
          mi += jp * std::log2(jp / (mi_val * mj_val));
        }
      }
    }
    return mi;

  } else if (t1 == column_type::CONTINUOUS && t2 == column_type::CONTINUOUS) {
    // KSG MI
    auto const &col1 = _continuous_cols[_continuous_col_index[attr1]];
    auto const &col2 = _continuous_cols[_continuous_col_index[attr2]];
    if (!_continuous_has_variation[_continuous_col_index[attr1]] ||
        !_continuous_has_variation[_continuous_col_index[attr2]]) {
      return 0.0;
    }
    return ksg_mi(col1.data(), col2.data(), _num_instances, _ksg_k);

  } else {
    // Ross (2014) mixed MI
    std::size_t disc_attr, cont_attr;
    if (t1 == column_type::DISCRETE) {
      disc_attr = attr1;
      cont_attr = attr2;
    } else {
      disc_attr = attr2;
      cont_attr = attr1;
    }
    auto const &disc_col = _discrete_cols[_discrete_col_index[disc_attr]];
    auto const &cont_col = _continuous_cols[_continuous_col_index[cont_attr]];

    if (_discrete_info[_discrete_col_index[disc_attr]].num_values() <= 1 ||
        !_continuous_has_variation[_continuous_col_index[cont_attr]]) {
      return 0.0;
    }

    return ross_mixed_mi(disc_col.data(), cont_col.data(), _num_instances, _ksg_k);
  }
}

#endif // MRMR_HAS_CONTINUOUS

#endif // MRMR_MIXED_DATASET_HPP
