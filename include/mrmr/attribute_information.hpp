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

#ifndef MRMR_ATTRIBUTE_INFORMATION_HPP
#define MRMR_ATTRIBUTE_INFORMATION_HPP

#include <array>
#include <cassert>
#include <iterator>
#include <limits>
#include <mrmr/typedef.hpp>
#include <valarray>

/**
 * @brief Computes and caches information-theoretic measures for a single attribute.
 *
 * Accepts a range of discretized values and precomputes the marginal probability
 * distribution and Shannon entropy for efficient repeated lookup. Input values
 * must be contiguous integers in [0, num_values), as produced by
 * dataset::transpose_and_discretize. Missing values are not supported; the caller
 * must ensure complete data before construction.
 *
 * @tparam T Unsigned integer type for attribute values. Must have
 *           std::numeric_limits<T>::max() <= 255 (typically unsigned char).
 */
template <typename T> class attribute_information {
  static_assert(std::numeric_limits<T>::max() <= 255,
                "attribute_information only supports types with max value <= 255");

public:
  /**
   * @brief Construct from a range of discretized attribute values.
   *
   * Computes the marginal probability distribution and Shannon entropy over
   * all values in [first, last). The range must be non-empty and values must
   * be contiguous integers in [0, k) for some k <= std::numeric_limits<T>::max().
   *
   * @tparam ForwardIterator Iterator type satisfying ForwardIterator requirements.
   * @param first Iterator to the first element of the attribute value range.
   * @param last  Iterator one past the last element of the attribute value range.
   */
  template <typename ForwardIterator>
  attribute_information(ForwardIterator first, ForwardIterator last);

  /** @brief Return the number of distinct values observed for this attribute. */
  T num_values() const;

  /** @brief Return the Shannon entropy of this attribute in bits. */
  double entropy() const;

  /**
   * @brief Return the marginal probability of a given value index.
   *
   * @param index Dense value index in [0, num_values()).
   * @return Marginal probability P(X = index).
   */
  probability marginal_probability(T index) const;

private:
  double _entropy;
  std::valarray<probability> _pdf;
};

template <typename T>
template <typename ForwardIterator>
attribute_information<T>::attribute_information(ForwardIterator first, ForwardIterator last) {
  // determine number of elements (std::distance is correct for all iterator categories)
  auto count = static_cast<std::size_t>(std::distance(first, last));

  // compute temporary histogram on fast integral type
  std::array<unsigned int, std::numeric_limits<T>::max() + 1> temp_histogram = {};
  while (first != last) {
    ++temp_histogram[*first];
    ++first;
  }

  // find non-zero values in histogram and populate storage-optimized final PDF
  T buckets = 0;
  for (auto it = std::cbegin(temp_histogram); it != std::cend(temp_histogram); ++it) {
    if (*it != 0) {
      ++buckets;
    }
  }
  _pdf.resize(buckets);
  std::copy_if(std::cbegin(temp_histogram), std::cend(temp_histogram), std::begin(_pdf),
               [](unsigned int freq) { return freq != 0; });
  _pdf = _pdf / static_cast<double>(count);

  // compute entropy
  _entropy = -1 * (_pdf * std::log(_pdf)).sum() / std::log(2);
}

template <typename T> T attribute_information<T>::num_values() const { return _pdf.size(); }

template <typename T> double attribute_information<T>::entropy() const { return _entropy; }

template <typename T> probability attribute_information<T>::marginal_probability(T index) const {
  assert(index < num_values());
  return _pdf[index];
}

#endif
