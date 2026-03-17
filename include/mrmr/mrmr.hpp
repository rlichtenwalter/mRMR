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

#ifndef MRMR_MRMR_HPP
#define MRMR_MRMR_HPP

#include <algorithm>
#include <cassert>
#include <cmath>
#include <forward_list>
#include <functional>
#include <limits>
#include <mrmr/dataset.hpp>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// Return type for mRMR call: tuple containing vectors of
// (ranks, attribute_indices, attribute_names, entropies, mutual_informations, mrmr_scores)
using mrmr_return_type =
    std::tuple<std::vector<std::size_t>, std::vector<std::size_t>, std::vector<std::string>,
               std::vector<double>, std::vector<double>, std::vector<double>>;

// Optional callback invoked after each rank is computed. Enables streaming output
// without buffering all results. Parameters: rank, attribute_index, name, entropy, mi, score.
using mrmr_rank_callback =
    std::function<void(std::size_t, std::size_t, std::string const &, double, double, double)>;

// Default threshold for precomputing the pairwise MI cache. When the number of
// useful (positive-entropy) attributes is at or below this value, all pairwise MI
// values are precomputed into a triangular cache for O(1) lookup during the
// selection loop. Above this threshold, MI is computed on-the-fly per pair to
// avoid O(M^2) memory consumption — critical for datasets with millions of attributes.
// At the default of 5000, the cache uses ~95MB (5000*4999/2 * 8 bytes).
constexpr std::size_t MRMR_DEFAULT_CACHE_THRESHOLD = 5000;

// Precomputed upper-triangular pairwise MI cache for efficient lookup.
// Maps original attribute indices to a dense index space, then stores MI values
// in a flat vector using triangular indexing. MI(X,Y) = MI(Y,X) so only
// the upper triangle is stored, halving memory relative to a full matrix.
// Only indices passed to the constructor via attr_indices may be used with get().
template <typename T> class triangular_mi_cache {
  static constexpr std::size_t UNMAPPED = std::numeric_limits<std::size_t>::max();

public:
  triangular_mi_cache(dataset<T> const &data, std::vector<std::size_t> const &attr_indices)
      : _m(attr_indices.size()) {
    // Guard against overflow in triangular indexing for very large M
    if (_m >= 2) {
      constexpr std::size_t limit = std::numeric_limits<std::size_t>::max();
      if ((_m - 2) > limit / (_m + 1)) {
        throw std::length_error("triangular_mi_cache: M too large for size_t indexing");
      }
    }

    // Build mapping from original attribute index to dense index.
    // Unmapped indices are set to UNMAPPED sentinel for safety.
    _to_dense.assign(data.num_attributes(), UNMAPPED);
    for (std::size_t i = 0; i < _m; ++i) {
      _to_dense[attr_indices[i]] = i;
    }

    // Precompute MI for all unique pairs
    _cache.resize(_m * (_m - 1) / 2);
    for (std::size_t i = 0; i < _m; ++i) {
      for (std::size_t j = i + 1; j < _m; ++j) {
        _cache[tri_index(i, j)] = data.mutual_information(attr_indices[i], attr_indices[j]);
      }
    }
  }

  double get(std::size_t a1, std::size_t a2) const {
    assert(a1 < _to_dense.size() && _to_dense[a1] != UNMAPPED);
    assert(a2 < _to_dense.size() && _to_dense[a2] != UNMAPPED);
    std::size_t d1 = _to_dense[a1];
    std::size_t d2 = _to_dense[a2];
    if (d1 == d2) {
      return 0.0;
    }
    if (d1 > d2) {
      std::swap(d1, d2);
    }
    return _cache[tri_index(d1, d2)];
  }

private:
  std::size_t tri_index(std::size_t i, std::size_t j) const {
    return i * (2 * _m - i - 1) / 2 + (j - i - 1);
  }

  std::size_t _m;
  std::vector<std::size_t> _to_dense;
  std::vector<double> _cache;
};

// Core mRMR selection loop, templated on the MI lookup callable to allow
// compile-time specialization for both cached (O(1)) and on-the-fly (O(N))
// MI computation without runtime dispatch overhead in the hot loop.
template <typename MILookup, typename OnSelected>
void mrmr_selection_loop(std::vector<double> const &mutual_informations,
                         std::vector<double> &redundance,
                         std::forward_list<std::size_t> &unselected,
                         std::size_t last_attribute_index, std::size_t start_rank,
                         MILookup &&get_mi, OnSelected &&on_selected) {
  std::size_t rank = start_rank;
  while (!unselected.empty()) {
    double best_mrmr_score = -std::numeric_limits<double>::infinity();
    std::size_t best_attribute_index = 0;
    auto it = std::cbegin(unselected);
    auto last_it = unselected.before_begin();
    auto erase_it = last_it;
    while (it != std::cend(unselected)) {
      std::size_t attribute_index = *it;
      redundance.at(attribute_index) += get_mi(last_attribute_index, attribute_index);
      double mrmr_score =
          mutual_informations.at(attribute_index) - redundance.at(attribute_index) / (rank - 1);
      if (mrmr_score - best_mrmr_score > std::numeric_limits<double>::epsilon()) {
        best_mrmr_score = mrmr_score;
        best_attribute_index = attribute_index;
        erase_it = last_it;
      }
      ++it;
      ++last_it;
    }

    on_selected(rank, best_attribute_index, best_mrmr_score);

    unselected.erase_after(erase_it);
    last_attribute_index = best_attribute_index;
    ++rank;
  }
}

// Compute mRMR feature ranking.
//
// MI caching strategy is selected dynamically based on the number of useful
// (positive-entropy) attributes M:
// - M <= cache_threshold (default 5000): precompute all M*(M-1)/2 pairwise MI
//   values into a triangular cache. The selection loop then does O(1) lookups.
//   At threshold 5000 this uses ~95MB.
// - M > cache_threshold: compute MI on-the-fly per pair (O(N) each) using the
//   reusable scratch buffer in dataset. Essential for very wide datasets
//   (millions of attributes) where O(M^2) memory is infeasible.
template <typename T>
mrmr_return_type mrmr(dataset<T> const &data, std::size_t class_attribute_index,
                      mrmr_rank_callback const &on_rank = nullptr,
                      std::size_t cache_threshold = MRMR_DEFAULT_CACHE_THRESHOLD) {

  mrmr_return_type retval;
  std::get<0>(retval).reserve(data.num_attributes());
  std::get<1>(retval).reserve(data.num_attributes());
  std::get<2>(retval).reserve(data.num_attributes());
  std::get<3>(retval).reserve(data.num_attributes());
  std::get<4>(retval).reserve(data.num_attributes());
  std::get<5>(retval).reserve(data.num_attributes());

  // Helper to record a rank and optionally invoke the callback
  auto emit_rank = [&](std::size_t rank, std::size_t index, std::string const &name, double entropy,
                       double mi, double score) {
    std::get<0>(retval).push_back(rank);
    std::get<1>(retval).push_back(index);
    std::get<2>(retval).push_back(name);
    std::get<3>(retval).push_back(entropy);
    std::get<4>(retval).push_back(mi);
    std::get<5>(retval).push_back(score);
    if (on_rank) {
      on_rank(rank, index, name, entropy, mi, score);
    }
  };

  // Compute mRMR prerequisites: MI with class for all positive-entropy attributes
  std::vector<double> mutual_informations(data.num_attributes());
  std::vector<double> redundance(data.num_attributes(), 0.0);
  std::forward_list<std::size_t> unselected;
  std::vector<std::size_t> useless;
  std::vector<std::size_t> useful_indices;
  for (std::size_t i = 0; i < data.num_attributes(); ++i) {
    if (i != class_attribute_index) {
      if (data.attribute_entropy(i) > 0) {
        mutual_informations[i] = data.mutual_information(class_attribute_index, i);
        unselected.push_front(i);
        useful_indices.push_back(i);
      } else {
        mutual_informations[i] = 0;
        useless.push_back(i);
      }
    }
  }
  unselected.reverse();
  mutual_informations[class_attribute_index] = -std::numeric_limits<double>::infinity();

  // Emit class attribute information (rank 0)
  double class_entropy = data.attribute_entropy(class_attribute_index);
  emit_rank(0, class_attribute_index, data.attribute_name(class_attribute_index), class_entropy,
            class_entropy, std::numeric_limits<double>::quiet_NaN());

  // First-rank selection and main selection loop only apply when there are useful attributes.
  // When all non-class attributes have zero entropy, skip directly to useless output.
  if (!useful_indices.empty()) {
    // Handle special case of first attribute with highest mutual information.
    // Note: when all useful attributes have zero MI with the class (degenerate case),
    // the selection among tied zero-MI attributes is arbitrary. This is defensible since
    // all candidates are equally uninformative. Subsequent redundance computation is
    // unaffected because MI with any zero-entropy attribute is 0.
    std::size_t best_attribute_index =
        std::max_element(mutual_informations.begin(), mutual_informations.end()) -
        mutual_informations.begin();
    std::size_t last_attribute_index = best_attribute_index;
    unselected.remove(best_attribute_index);
    double mrmr_score = mutual_informations.at(best_attribute_index);

    emit_rank(1, best_attribute_index, data.attribute_name(best_attribute_index),
              data.attribute_entropy(best_attribute_index), mrmr_score, mrmr_score);

    // Selection loop callback: emit each rank as it's computed
    auto on_selected = [&](std::size_t r, std::size_t attr_index, double score) {
      emit_rank(r, attr_index, data.attribute_name(attr_index), data.attribute_entropy(attr_index),
                mutual_informations.at(attr_index), score);
    };

    // Choose MI lookup strategy based on number of useful attributes.
    // For moderate M: precompute all pairwise MI into triangular cache (O(1) lookup).
    // For very large M: compute on-the-fly to avoid O(M^2) memory (O(N) per lookup).
    if (useful_indices.size() <= cache_threshold && useful_indices.size() > 1) {
      triangular_mi_cache<T> cache(data, useful_indices);
      mrmr_selection_loop(
          mutual_informations, redundance, unselected, last_attribute_index, 2,
          [&cache](std::size_t a1, std::size_t a2) { return cache.get(a1, a2); }, on_selected);
    } else if (useful_indices.size() > 1) {
      mrmr_selection_loop(
          mutual_informations, redundance, unselected, last_attribute_index, 2,
          [&data](std::size_t a1, std::size_t a2) { return data.mutual_information(a1, a2); },
          on_selected);
    }
  }

  // Append useless features (zero-entropy attributes).
  // Useful attributes occupy ranks 1 through useful_indices.size(); useless follow.
  std::size_t useless_rank = useful_indices.size() + 1;
  std::sort(useless.begin(), useless.end());
  for (auto attribute_index : useless) {
    emit_rank(useless_rank++, attribute_index, data.attribute_name(attribute_index), 0, 0,
              -std::numeric_limits<double>::infinity());
  }

  return retval;
}

#endif
