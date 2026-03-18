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

#ifndef MRMR_MI_POLICY_HPP
#define MRMR_MI_POLICY_HPP

#include <cmath>
#include <cstddef>
#include <mrmr/attribute_information.hpp>
#include <vector>

/**
 * @brief MI accumulation policy for unweighted (uniform) instance contribution.
 *
 * Uses integer histogram for maximum throughput. The include() method always
 * returns true and is optimized away by the compiler, producing identical
 * codegen to a hand-written loop without the policy abstraction.
 */
struct unweighted_policy {
  using histogram_type = std::size_t;

  /** @brief All instances are included (compiler eliminates the check). */
  bool include(std::size_t /*inst*/) const { return true; }

  /** @brief Increment histogram bin by 1 (integer increment). */
  void accumulate(histogram_type &cell, std::size_t /*inst*/) const { ++cell; }

  /** @brief Convert histogram count to probability via reciprocal multiply. */
  double normalize(histogram_type count, double inv_n) const {
    return static_cast<double>(count) * inv_n;
  }
};

/**
 * @brief MI accumulation policy for weighted instances (e.g., bootstrap frequency).
 *
 * Uses double histogram. Each instance contributes its weight to the histogram
 * bin. The total_weight is precomputed at construction for normalization.
 *
 * @note Floating-point histogram accumulation is ~3-4x slower than integer
 *       increment due to FP add latency and dependency chains on hot bins.
 */
struct weighted_policy {
  using histogram_type = double;

  double const *weights;
  double total_weight;

  /**
   * @brief Construct from a weight vector.
   *
   * @param w       Pointer to weight array (must outlive this policy).
   * @param total_w Sum of all weights (precomputed by caller).
   */
  weighted_policy(double const *w, double total_w) : weights(w), total_weight(total_w) {}

  /** @brief All instances are included (weights handle contribution). */
  bool include(std::size_t /*inst*/) const { return true; }

  /** @brief Add the instance's weight to the histogram bin. */
  void accumulate(histogram_type &cell, std::size_t inst) const { cell += weights[inst]; }

  /** @brief Convert accumulated weight to probability by dividing by total weight. */
  double normalize(histogram_type count, double /*inv_n*/) const { return count / total_weight; }
};

/**
 * @brief Compute mutual information between two attributes of a data source.
 *
 * Templated on the MI accumulation policy for zero-overhead dispatch between
 * unweighted (integer histogram) and weighted (float histogram) paths. The
 * compiler inlines the policy methods, producing specialized code for each.
 *
 * @tparam DataSource Type satisfying the data source concept (operator(), num_instances(),
 *                    attribute_information via attr_info()).
 * @tparam Policy     MI accumulation policy (unweighted_policy or weighted_policy).
 * @param data   The data source.
 * @param info1  Attribute information for attribute1.
 * @param info2  Attribute information for attribute2.
 * @param attr1  Index of the first attribute.
 * @param attr2  Index of the second attribute.
 * @param policy The accumulation policy instance.
 * @return Mutual information I(attr1; attr2) >= 0, in bits.
 */
template <typename DataSource, typename Policy>
double compute_mi(DataSource const &data,
                  attribute_information<typename DataSource::value_type> const &info1,
                  attribute_information<typename DataSource::value_type> const &info2,
                  std::size_t attr1, std::size_t attr2, Policy const &policy) {
  std::size_t a1_num_values = info1.num_values();
  std::size_t a2_num_values = info2.num_values();
  if (a1_num_values == 1 || a2_num_values == 1) {
    return 0.0;
  }

  // Build joint histogram using local scratch buffer
  // Note: thread_local was considered but causes destruction-order issues at program exit
  // when multiple template instantiations exist. Per-call allocation is acceptable because
  // the histogram is small (typically < 64KB) and the MI computation dominates runtime.
  std::size_t histogram_size = a1_num_values * a2_num_values;
  std::vector<typename Policy::histogram_type> scratch(histogram_size,
                                                       typename Policy::histogram_type{});

  for (std::size_t i = 0; i < data.num_instances(); ++i) {
    if (policy.include(i)) {
      policy.accumulate(scratch[data(attr1, i) * a2_num_values + data(attr2, i)], i);
    }
  }

  // Compute mutual information from histogram
  double inv_n = 1.0 / static_cast<double>(data.num_instances());
  double mi = 0.0;
  for (std::size_t i = 0; i < a1_num_values; ++i) {
    for (std::size_t j = 0; j < a2_num_values; ++j) {
      auto count = scratch[i * a2_num_values + j];
      double joint_prob = policy.normalize(count, inv_n);
      if (joint_prob > 0) {
        double marginal_i = info1.marginal_probability(i);
        double marginal_j = info2.marginal_probability(j);
        mi += joint_prob * std::log2(joint_prob / (marginal_i * marginal_j));
      }
    }
  }
  return mi;
}

#endif
