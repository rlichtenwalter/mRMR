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
#include <mrmr/delimiter_ctype.hpp>
#include <mrmr/matrix.hpp>
#include <mrmr/typedef.hpp>
#include <stdexcept>
#include <valarray>
#include <vector>

// Dataset container for discretized tabular data with cached information-theoretic measures.
// T must be an unsigned integer type with max value <= 255 (typically unsigned char).
// Input data must be complete (no missing values). After construction, attribute values are
// guaranteed to be contiguous integers in [0, k) where k is the number of distinct values.
// This class is not thread-safe for concurrent mutual_information() calls on the same instance.
template <typename T> class dataset {
  static_assert(std::numeric_limits<T>::max() <= 255,
                "dataset only supports storage types with max value <= 255");

  template <typename U> friend std::ostream &operator<<(std::ostream &os, dataset<U> const &m);

private:
  using itype = long;
  using fptype = double;

public:
  using value_type = T;
  enum discretization_method : char { ROUND = 0, FLOOR = 1, CEILING = 2, TRUNCATE = 3 };
  dataset();
  dataset(std::istream &, discretization_method dm = ROUND, char delimiter = '\t');
  template <typename U>
  dataset(std::vector<U> data, std::size_t num_instances, std::size_t num_attributes,
          bool column_major = false, std::vector<std::string> names = std::vector<std::string>(),
          discretization_method dm = ROUND, char delimiter = '\t');
  std::size_t num_instances() const;
  std::size_t num_attributes() const;
  std::string attribute_name(std::size_t attribute_num) const;
  double attribute_entropy(std::size_t attribute_num) const;
  double mutual_information(std::size_t attribute1, std::size_t attribute2) const;

private:
  template <typename U>
  void transpose_and_discretize(matrix<U> const &temp, discretization_method dm);
  void compute_attribute_information();
  std::vector<std::string> _names;
  std::vector<attribute_information<T>> _attr_info;
  matrix<T> _data;
  char _delimiter;
  mutable std::vector<std::size_t> _mi_scratch;
};

template <typename T>
template <typename U>
void dataset<T>::transpose_and_discretize(matrix<U> const &temp, discretization_method dm) {
  // Discretize, transpose to column-major storage, translate to non-negative values,
  // and compact to contiguous unsigned integer indices for efficient histogram computation.

  auto discretize_value = [dm](U value) -> itype {
    switch (dm) {
    case ROUND:
      return static_cast<itype>(std::round(value));
    case FLOOR:
      return static_cast<itype>(std::floor(value));
    case CEILING:
      return static_cast<itype>(std::ceil(value));
    case TRUNCATE:
    default:
      return static_cast<itype>(std::trunc(value));
    }
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
      if (val < minima[attr]) {
        minima[attr] = val;
      }
      if (val > maxima[attr]) {
        maxima[attr] = val;
      }
    }
  }

  // Range check: each attribute's value span must fit in T after translation.
  for (std::size_t attr = 0; attr < n_attr; ++attr) {
    if (minima[attr] > maxima[attr]) {
      continue; // empty attribute (no instances)
    }
    if (maxima[attr] - minima[attr] > std::numeric_limits<T>::max()) {
      throw std::runtime_error("attribute '" + attribute_name(attr) + "' range (" +
                               std::to_string(maxima[attr] - minima[attr]) + ") exceeds " +
                               std::to_string(static_cast<int>(std::numeric_limits<T>::max())) +
                               " under current discretization");
    }
  }

  // Pass 2: Translate to [0, max-min], then compact non-contiguous values to dense
  // contiguous indices 0..k-1. Store results in column-major _data matrix.
  _data = matrix<T>(n_attr, n_inst);

  for (std::size_t attr = 0; attr < n_attr; ++attr) {
    itype range = maxima[attr] - minima[attr];

    // Build histogram of translated values for this attribute
    std::vector<unsigned int> histogram(static_cast<std::size_t>(range) + 1, 0);
    for (std::size_t inst = 0; inst < n_inst; ++inst) {
      itype translated = discretized[attr * n_inst + inst] - minima[attr];
      ++histogram[static_cast<std::size_t>(translated)];
    }

    // Build rank map: maps each populated translated value to a dense index
    std::vector<T> rank_map(static_cast<std::size_t>(range) + 1, 0);
    T rank = 0;
    for (std::size_t v = 0; v <= static_cast<std::size_t>(range); ++v) {
      if (histogram[v] > 0) {
        rank_map[v] = rank++;
      }
    }

    // Store compacted values
    for (std::size_t inst = 0; inst < n_inst; ++inst) {
      itype translated = discretized[attr * n_inst + inst] - minima[attr];
      _data(attr, inst) = rank_map[static_cast<std::size_t>(translated)];
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

template <typename T> dataset<T>::dataset() : _data(0, 0), _delimiter('\t') {}

template <typename T>
dataset<T>::dataset(std::istream &is, discretization_method dm, char delimiter)
    : _delimiter(delimiter) {
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

  // read data matrix
  matrix<fptype> temp;
  temp.set_delimiter(_delimiter);
  is >> temp;

  transpose_and_discretize(temp, dm);

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

  transpose_and_discretize(temp, dm);

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

template <typename T>
double dataset<T>::mutual_information(std::size_t attribute1, std::size_t attribute2) const {
  std::size_t a1_num_values = _attr_info.at(attribute1).num_values();
  std::size_t a2_num_values = _attr_info.at(attribute2).num_values();
  if (a1_num_values == 1 || a2_num_values == 1) {
    return 0.0;
  }

  // Build joint histogram using reusable scratch buffer
  std::size_t histogram_size = a1_num_values * a2_num_values;
  _mi_scratch.assign(histogram_size, 0);
  for (std::size_t i = 0; i < num_instances(); ++i) {
    ++_mi_scratch[_data(attribute1, i) * a2_num_values + _data(attribute2, i)];
  }

  // Compute mutual information directly from integer histogram
  double inv_n = 1.0 / static_cast<double>(num_instances());
  double mi = 0.0;
  for (std::size_t i = 0; i < a1_num_values; ++i) {
    for (std::size_t j = 0; j < a2_num_values; ++j) {
      std::size_t count = _mi_scratch[i * a2_num_values + j];
      if (count != 0) {
        double joint_prob = count * inv_n;
        probability marginal_i = _attr_info.at(attribute1).marginal_probability(i);
        probability marginal_j = _attr_info.at(attribute2).marginal_probability(j);
        mi += joint_prob * std::log2(joint_prob / (marginal_i * marginal_j));
      }
    }
  }
  return mi;
}

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
