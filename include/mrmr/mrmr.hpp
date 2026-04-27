// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2018-2026 Ryan N. Lichtenwalter

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

/**
 * @brief Return type for a complete mRMR feature ranking.
 *
 * A tuple of six parallel vectors, each with one entry per ranked attribute:
 * - [0] ranks           — 0-based rank positions (0 = class attribute).
 * - [1] attribute indices — original attribute column indices in the dataset.
 * - [2] attribute names  — attribute name strings.
 * - [3] entropies        — Shannon entropy of each attribute in bits.
 * - [4] mutual_informations — MI with the class attribute in bits.
 * - [5] mrmr_scores      — final mRMR relevance-minus-redundancy score.
 */
using mrmr_return_type =
    std::tuple<std::vector<std::size_t>, std::vector<std::size_t>, std::vector<std::string>,
               std::vector<double>, std::vector<double>, std::vector<double>>;

/**
 * @brief Optional callback invoked once per ranked attribute during mRMR selection.
 *
 * Enables streaming output of results without buffering the full return tuple.
 * The parameters correspond to: rank, attribute_index, name, entropy,
 * mutual_information_with_class, mrmr_score.
 */
using mrmr_rank_callback =
    std::function<void(std::size_t, std::size_t, std::string const &, double, double, double)>;

/**
 * @brief Default threshold for precomputing the pairwise MI cache.
 *
 * When the number of useful (positive-entropy) attributes M is at or below this
 * value, all M*(M-1)/2 pairwise MI values are precomputed into a triangular cache
 * for O(1) lookup during the selection loop. Above this threshold, MI is computed
 * on-the-fly per pair to avoid O(M^2) memory consumption — critical for datasets
 * with millions of attributes. At the default of 5000, the cache uses approximately
 * 95 MB (5000 * 4999 / 2 * 8 bytes).
 */
constexpr std::size_t MRMR_DEFAULT_CACHE_THRESHOLD = 5000;

/**
 * @brief Precomputed upper-triangular pairwise MI cache for efficient O(1) lookup.
 *
 * Maps original attribute indices to a dense index space, then stores MI values
 * in a flat vector using triangular indexing. Because MI(X, Y) == MI(Y, X), only
 * the upper triangle is stored, halving memory relative to a full matrix. Only
 * indices passed to the constructor via @p attr_indices may be used with get().
 *
 * @tparam T Element storage type of the source dataset.
 */
template <typename DataSource> class triangular_mi_cache {
  static std::size_t unmapped_sentinel() { return std::numeric_limits<std::size_t>::max(); }

public:
  /**
   * @brief Construct the cache by precomputing all pairwise MI values.
   *
   * Builds a mapping from original attribute indices to dense indices and
   * precomputes MI for every unique pair (i, j) with i < j.
   *
   * @param data         Dataset providing the mutual_information() method.
   * @param attr_indices Indices of the attributes to cache; must be valid
   *                     indices into @p data.
   * @throws std::length_error If attr_indices.size() is too large for
   *                           triangular indexing with std::size_t arithmetic.
   */
  triangular_mi_cache(DataSource const &data, std::vector<std::size_t> const &attr_indices)
      : _m(attr_indices.size()) {
    // Guard against overflow in triangular indexing for very large M
    if (_m >= 2) {
      constexpr std::size_t limit = std::numeric_limits<std::size_t>::max();
      if ((_m - 2) > limit / (_m + 1)) {
        throw std::length_error("triangular_mi_cache: M too large for size_t indexing");
      }
    }

    // Build mapping from original attribute index to dense index.
    // Unmapped indices are set to unmapped_sentinel() sentinel for safety.
    _to_dense.assign(data.num_attributes(), unmapped_sentinel());
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

  /**
   * @brief Return the precomputed MI between two attributes.
   *
   * Both @p a1 and @p a2 must have been included in @p attr_indices at
   * construction time. Returns 0 when @p a1 == @p a2.
   *
   * @param a1 Original attribute index of the first attribute.
   * @param a2 Original attribute index of the second attribute.
   * @return Precomputed mutual information I(a1; a2) >= 0.
   */
  double get(std::size_t a1, std::size_t a2) const {
    assert(a1 < _to_dense.size() && _to_dense[a1] != unmapped_sentinel());
    assert(a2 < _to_dense.size() && _to_dense[a2] != unmapped_sentinel());
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

/**
 * @brief Core mRMR selection loop, templated on the MI lookup callable.
 *
 * Iteratively selects the unselected attribute with the highest
 * relevance-minus-redundancy score until no unselected attributes remain.
 * Templating on @p MILookup allows compile-time specialization for both cached
 * (O(1)) and on-the-fly (O(N)) MI computation without runtime dispatch overhead
 * in the hot loop.
 *
 * @tparam MILookup    Callable with signature double(size_t, size_t) returning
 *                     MI between two attribute indices.
 * @tparam OnSelected  Callable with signature void(size_t rank, size_t attr_index,
 *                     double mrmr_score) invoked after each selection.
 * @param mutual_informations  Per-attribute MI with the class (indexed by attribute index).
 * @param redundance           Accumulated redundance accumulator (updated in-place).
 * @param unselected           Forward list of attribute indices not yet selected
 *                             (modified in-place; selected attribute is erased).
 * @param last_attribute_index Index of the most recently selected attribute.
 * @param start_rank           Rank value assigned to the first selection in this call.
 * @param get_mi               MI lookup callable.
 * @param on_selected          Callback invoked once per selected attribute.
 */
template <typename MILookup, typename OnSelected>
void mrmr_selection_loop(std::vector<double> const &mutual_informations,
                         std::vector<double> &redundance,
                         std::forward_list<std::size_t> &unselected,
                         std::size_t last_attribute_index, std::size_t start_rank,
                         MILookup &&get_mi, OnSelected &&on_selected) {
  assert(start_rank >= 2 && "start_rank must be >= 2 to avoid division by zero in redundance");
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
      double mrmr_score = mutual_informations.at(attribute_index) -
                          redundance.at(attribute_index) / static_cast<double>(rank - 1);
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

/**
 * @brief Compute a complete mRMR feature ranking for the given dataset.
 *
 * Ranks all non-class attributes by their minimum-Redundancy Maximum-Relevance
 * score. The class attribute is emitted at rank 0. Attributes with zero entropy
 * (constant values) are ranked last with a score of -infinity.
 *
 * The MI caching strategy is selected dynamically based on the number of useful
 * (positive-entropy) attributes M:
 * - M <= @p cache_threshold: precompute all M*(M-1)/2 pairwise MI values into a
 *   triangular cache. The selection loop then performs O(1) lookups. At the
 *   default threshold of 5000 this uses approximately 95 MB.
 * - M > @p cache_threshold: compute MI on-the-fly per pair (O(N) each) using the
 *   reusable scratch buffer in dataset. Essential for very wide datasets
 *   (millions of attributes) where O(M^2) memory is infeasible.
 *
 * @tparam DataSource Data source type satisfying the DataSource concept
 *                   (num_instances(), num_attributes(), attribute_name(),
 *                    attribute_entropy(), mutual_information(), operator()).
 * @param data                  Data source to rank.
 * @param class_attribute_index Index of the class attribute within the data source.
 * @param on_rank               Optional callback invoked once per ranked attribute;
 *                              pass nullptr to disable streaming output.
 * @param cache_threshold       Maximum number of useful attributes for which the
 *                              triangular MI cache is precomputed.
 * @return mrmr_return_type containing six parallel vectors of per-rank metadata.
 */
template <typename DataSource>
mrmr_return_type mrmr(DataSource const &data, std::size_t class_attribute_index,
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
    // Search only useful_indices (not the full vector) to prevent selecting
    // a useless (zero-entropy) attribute that happens to have MI=0 ahead of
    // useful attributes that also have MI=0.
    std::size_t best_attribute_index = useful_indices[0];
    for (std::size_t idx : useful_indices) {
      if (mutual_informations[idx] > mutual_informations[best_attribute_index]) {
        best_attribute_index = idx;
      }
    }
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
      triangular_mi_cache<DataSource> cache(data, useful_indices);
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
  std::ranges::sort(useless);
  for (auto attribute_index : useless) {
    emit_rank(useless_rank++, attribute_index, data.attribute_name(attribute_index), 0, 0,
              -std::numeric_limits<double>::infinity());
  }

  return retval;
}

#endif
