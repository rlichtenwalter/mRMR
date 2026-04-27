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

#ifndef MRMR_DATASET_VIEW_HPP
#define MRMR_DATASET_VIEW_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mrmr/attribute_information.hpp>
#include <mrmr/dataset.hpp>
#include <mrmr/mi_policy.hpp>
#include <mrmr/typedef.hpp>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief Non-owning view over a dataset with instance and attribute subsetting.
 *
 * Provides the same public interface as dataset<T> for use with templated
 * algorithms (e.g., mrmr()). Accesses the parent dataset's data through
 * sorted instance indices for cache-efficient bootstrap resampling, and
 * optional attribute indices for feature subspacing. No data is copied;
 * only index vectors and recomputed attribute statistics are owned.
 *
 * When instance weights are provided (e.g., from bootstrap frequency counting),
 * mutual information is computed using weighted histograms via weighted_policy.
 * When no weights are provided, the unweighted integer-histogram path is used.
 *
 * Instance indices are sorted at construction time for cache-friendly access
 * patterns (see README.md Design Notes for benchmarking rationale).
 *
 * @warning The parent dataset must outlive the view. Destroying the parent
 *          while views exist results in undefined behavior.
 *
 * @tparam T Unsigned integer storage type matching the parent dataset.
 */
template <typename T> class dataset_view {
public:
  using value_type = T;

  /**
   * @brief Construct a view with explicit instance and attribute indices.
   *
   * @param source           Parent dataset (must outlive this view).
   * @param instance_indices Instance indices into the parent (duplicates allowed for bootstrap).
   * @param attribute_indices Attribute indices into the parent (no duplicates).
   *                          If empty, all parent attributes are included.
   * @param weights          Per-instance weights. If empty, uniform weights are assumed.
   *                          Must be same size as instance_indices if non-empty.
   */
  dataset_view(dataset<T> const &source, std::vector<std::size_t> instance_indices,
               std::vector<std::size_t> attribute_indices = {}, std::vector<double> weights = {});

  /** @brief Return the number of instances in this view (may include duplicates). */
  std::size_t num_instances() const { return _instance_indices.size(); }

  /** @brief Return the number of attributes in this view. */
  std::size_t num_attributes() const { return _attribute_indices.size(); }

  /**
   * @brief Access a single discretized cell value through index indirection.
   *
   * @param attribute View-local attribute index in [0, num_attributes()).
   * @param instance  View-local instance index in [0, num_instances()).
   * @return The discretized value from the parent dataset.
   */
  T operator()(std::size_t attribute, std::size_t instance) const {
    return _source((_attribute_indices[attribute]), _instance_indices[instance]);
  }

  /** @brief Return the name of the attribute at the given view-local index. */
  std::string attribute_name(std::size_t attribute_num) const {
    return _source.attribute_name(_attribute_indices[attribute_num]);
  }

  /** @brief Return the Shannon entropy of the given view-local attribute. */
  double attribute_entropy(std::size_t attribute_num) const {
    return _attr_info[attribute_num].entropy();
  }

  /**
   * @brief Compute mutual information between two view-local attributes.
   *
   * Uses weighted_policy if weights were provided at construction, otherwise
   * uses unweighted_policy with integer histograms.
   */
  double mutual_information(std::size_t attribute1, std::size_t attribute2) const;

  // --- Static factory methods for common view construction patterns ---

  /**
   * @brief Create a bootstrap resample view (sampling with replacement).
   *
   * @param source Parent dataset.
   * @param gen    Random number generator.
   * @return A view with N randomly sampled instance indices (with replacement).
   */
  static dataset_view bootstrap(dataset<T> const &source, std::mt19937 &gen);

  /**
   * @brief Create a stratified bootstrap view preserving class distribution.
   *
   * @param source     Parent dataset.
   * @param class_attr Index of the class attribute in the parent dataset.
   * @param gen        Random number generator.
   * @return A view with N stratified-sampled instance indices.
   */
  static dataset_view stratified_bootstrap(dataset<T> const &source, std::size_t class_attr,
                                           std::mt19937 &gen);

  /**
   * @brief Create a feature subspace view (random attribute subset).
   *
   * @param source       Parent dataset.
   * @param num_features Number of attributes to randomly select.
   * @param gen          Random number generator.
   * @return A view with all instances but only num_features attributes.
   */
  static dataset_view subspace(dataset<T> const &source, std::size_t num_features,
                               std::mt19937 &gen);

private:
  void compute_attribute_information();

  dataset<T> const &_source;
  std::vector<std::size_t> _instance_indices;
  std::vector<std::size_t> _attribute_indices;
  std::vector<double> _weights;
  std::vector<attribute_information<T>> _attr_info;
};

// ============================================================================
// Implementation
// ============================================================================

template <typename T>
dataset_view<T>::dataset_view(dataset<T> const &source, std::vector<std::size_t> instance_indices,
                              std::vector<std::size_t> attribute_indices,
                              std::vector<double> weights)
    : _source(source), _instance_indices(std::move(instance_indices)),
      _attribute_indices(std::move(attribute_indices)), _weights(std::move(weights)) {
  // Default: all attributes if none specified
  if (_attribute_indices.empty()) {
    _attribute_indices.resize(source.num_attributes());
    std::iota(_attribute_indices.begin(), _attribute_indices.end(), 0);
  }

  // Validate weights
  if (!_weights.empty() && _weights.size() != _instance_indices.size()) {
    throw std::logic_error("weights size must match instance_indices size");
  }

  // Sort instance indices for cache-friendly access (skip for small N)
  if (_instance_indices.size() > 10000) {
    if (_weights.empty()) {
      std::ranges::sort(_instance_indices);
    } else {
      // Sort indices and weights together
      std::vector<std::size_t> order(_instance_indices.size());
      std::iota(order.begin(), order.end(), 0);
      std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        return _instance_indices[a] < _instance_indices[b];
      });
      std::vector<std::size_t> sorted_indices(_instance_indices.size());
      std::vector<double> sorted_weights(_weights.size());
      for (std::size_t i = 0; i < order.size(); ++i) {
        sorted_indices[i] = _instance_indices[order[i]];
        sorted_weights[i] = _weights[order[i]];
      }
      _instance_indices = std::move(sorted_indices);
      _weights = std::move(sorted_weights);
    }
  }

  compute_attribute_information();
}

template <typename T> void dataset_view<T>::compute_attribute_information() {
  _attr_info.reserve(num_attributes());
  for (std::size_t attr = 0; attr < num_attributes(); ++attr) {
    // Gather attribute values through the instance index indirection
    std::vector<T> values(num_instances());
    for (std::size_t inst = 0; inst < num_instances(); ++inst) {
      values[inst] = (*this)(attr, inst);
    }
    _attr_info.emplace_back(values.begin(), values.end());
  }
}

template <typename T>
double dataset_view<T>::mutual_information(std::size_t attribute1, std::size_t attribute2) const {
  // Use the SOURCE dataset's attribute_information for histogram sizing because
  // the cell values (from operator()) are in the source's compacted index space.
  // The view's resampled subset may have fewer distinct values, but the values
  // themselves are still indices into the source's [0, k) range.
  std::size_t src_attr1 = _attribute_indices[attribute1];
  std::size_t src_attr2 = _attribute_indices[attribute2];
  auto const &src_info1 = _source._attr_info[src_attr1];
  auto const &src_info2 = _source._attr_info[src_attr2];

  if (_weights.empty()) {
    return compute_mi(*this, src_info1, src_info2, attribute1, attribute2, unweighted_policy{});
  } else {
    double total_weight = 0;
    for (auto w : _weights) {
      total_weight += w;
    }
    return compute_mi(*this, src_info1, src_info2, attribute1, attribute2,
                      weighted_policy{_weights.data(), total_weight});
  }
}

// --- Static factories ---

template <typename T>
dataset_view<T> dataset_view<T>::bootstrap(dataset<T> const &source, std::mt19937 &gen) {
  if (source.num_instances() == 0) {
    return dataset_view<T>(source, {});
  }
  std::uniform_int_distribution<std::size_t> dist(0, source.num_instances() - 1);
  std::vector<std::size_t> indices(source.num_instances());
  for (auto &idx : indices) {
    idx = dist(gen);
  }
  return dataset_view<T>(source, std::move(indices));
}

template <typename T>
dataset_view<T> dataset_view<T>::stratified_bootstrap(dataset<T> const &source,
                                                      std::size_t class_attr, std::mt19937 &gen) {
  // Group instances by class value, using the actual number of distinct values
  std::size_t num_classes = source._attr_info[class_attr].num_values();
  if (num_classes == 0) {
    num_classes = 1;
  }
  std::vector<std::vector<std::size_t>> class_instances(num_classes);
  for (std::size_t i = 0; i < source.num_instances(); ++i) {
    class_instances[source(class_attr, i)].push_back(i);
  }

  // Sample proportionally from each class
  std::vector<std::size_t> indices;
  indices.reserve(source.num_instances());
  for (auto const &group : class_instances) {
    if (group.empty()) {
      continue;
    }
    std::uniform_int_distribution<std::size_t> dist(0, group.size() - 1);
    for (std::size_t i = 0; i < group.size(); ++i) {
      indices.push_back(group[dist(gen)]);
    }
  }

  return dataset_view<T>(source, std::move(indices));
}

template <typename T>
dataset_view<T> dataset_view<T>::subspace(dataset<T> const &source, std::size_t num_features,
                                          std::mt19937 &gen) {
  if (num_features > source.num_attributes()) {
    throw std::logic_error("num_features exceeds num_attributes");
  }

  // Randomly select num_features attribute indices without replacement
  std::vector<std::size_t> all_attrs(source.num_attributes());
  std::iota(all_attrs.begin(), all_attrs.end(), 0);
  std::shuffle(all_attrs.begin(), all_attrs.end(), gen);
  all_attrs.resize(num_features);
  std::ranges::sort(all_attrs); // keep attribute order

  // All instances
  std::vector<std::size_t> all_instances(source.num_instances());
  std::iota(all_instances.begin(), all_instances.end(), 0);

  return dataset_view<T>(source, std::move(all_instances), std::move(all_attrs));
}

#endif
