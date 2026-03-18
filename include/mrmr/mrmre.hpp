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

#ifndef MRMR_MRMRE_HPP
#define MRMR_MRMRE_HPP

#include <algorithm>
#include <cstddef>
#include <limits>
#include <mrmr/dataset.hpp>
#include <mrmr/dataset_view.hpp>
#include <mrmr/mrmr.hpp>
#include <numeric>
#include <random>
#include <string>
#include <tuple>
#include <vector>

/**
 * @brief Result of a single mRMR solution within an ensemble.
 */
struct mrmre_solution {
  std::vector<std::size_t> selected_indices;
  std::vector<double> scores;
};

/**
 * @brief Result of an mRMRe ensemble feature selection.
 */
struct mrmre_result {
  std::vector<mrmre_solution> solutions;
  std::vector<std::size_t> consensus_ranking;
  std::vector<std::size_t> feature_frequencies;
};

/** @brief Ensemble method for generating multiple mRMR solutions. */
enum class mrmre_method {
  EXHAUSTIVE, ///< Each solution starts from a different top-k relevant feature.
  BOOTSTRAP   ///< Each solution runs on a bootstrap resample of the instances.
};

/**
 * @brief Build consensus ranking from multiple mRMR solutions.
 *
 * Features are ranked by selection frequency (descending), with ties broken
 * by average mRMR score across solutions containing the feature.
 */
inline std::pair<std::vector<std::size_t>, std::vector<std::size_t>>
build_consensus(std::vector<mrmre_solution> const &solutions, std::size_t num_attributes) {
  std::vector<std::size_t> frequencies(num_attributes, 0);
  std::vector<double> avg_scores(num_attributes, 0.0);

  for (auto const &sol : solutions) {
    for (std::size_t i = 0; i < sol.selected_indices.size(); ++i) {
      std::size_t idx = sol.selected_indices[i];
      ++frequencies[idx];
      avg_scores[idx] += sol.scores[i];
    }
  }

  for (std::size_t i = 0; i < num_attributes; ++i) {
    if (frequencies[i] > 0) {
      avg_scores[i] /= static_cast<double>(frequencies[i]);
    }
  }

  std::vector<std::size_t> ranking(num_attributes);
  std::iota(ranking.begin(), ranking.end(), 0);
  std::sort(ranking.begin(), ranking.end(), [&](std::size_t a, std::size_t b) {
    if (frequencies[a] != frequencies[b]) {
      return frequencies[a] > frequencies[b];
    }
    return avg_scores[a] > avg_scores[b];
  });

  return {ranking, frequencies};
}

/**
 * @brief Extract a single mRMRe solution from an mRMR result.
 *
 * Skips rank 0 (class attribute), takes up to feature_count features.
 */
inline mrmre_solution extract_solution(mrmr_return_type const &result, std::size_t feature_count) {
  mrmre_solution sol;
  auto const &indices = std::get<1>(result);
  auto const &scores = std::get<5>(result);
  for (std::size_t i = 1; i < indices.size() && sol.selected_indices.size() < feature_count; ++i) {
    sol.selected_indices.push_back(indices[i]);
    sol.scores.push_back(scores[i]);
  }
  return sol;
}

/**
 * @brief Perform mRMRe ensemble feature selection.
 *
 * **Exhaustive method:** Generates one solution per top-k relevant feature,
 * each seeded by running mRMR on a view where only that feature and the
 * remaining candidates are available (the seed feature is naturally selected
 * first because it has the highest MI with the class by construction).
 *
 * **Bootstrap method:** Generates solution_count bootstrap resamples of the
 * dataset instances using dataset_view, running standard mRMR on each.
 *
 * @tparam T Dataset storage type.
 * @param data                  Source dataset.
 * @param class_attribute_index Index of the class/target attribute.
 * @param feature_count         Number of features to select per solution.
 * @param solution_count        Number of ensemble solutions to generate.
 * @param method                Ensemble method (EXHAUSTIVE or BOOTSTRAP).
 * @param seed                  Random seed for bootstrap resampling.
 * @param cache_threshold       MI cache threshold passed to underlying mRMR calls.
 * @return mrmre_result with all solutions and consensus ranking.
 */
template <typename T>
mrmre_result mrmre(dataset<T> const &data, std::size_t class_attribute_index,
                   std::size_t feature_count, std::size_t solution_count,
                   mrmre_method method = mrmre_method::EXHAUSTIVE, unsigned seed = 42,
                   std::size_t cache_threshold = MRMR_DEFAULT_CACHE_THRESHOLD) {
  mrmre_result result;

  if (method == mrmre_method::EXHAUSTIVE) {
    // Compute MI with class for all useful attributes, rank by MI
    std::vector<std::pair<double, std::size_t>> mi_ranked;
    for (std::size_t i = 0; i < data.num_attributes(); ++i) {
      if (i != class_attribute_index && data.attribute_entropy(i) > 0) {
        double mi = data.mutual_information(class_attribute_index, i);
        mi_ranked.emplace_back(mi, i);
      }
    }
    std::sort(mi_ranked.begin(), mi_ranked.end(),
              [](auto const &a, auto const &b) { return a.first > b.first; });

    // Generate one solution per top-k seed feature, each forced to start
    // with a different feature. We precompute class-MI for all useful attrs,
    // then for each seed, force it as rank-1 and run the selection loop.
    std::size_t num_solutions = std::min(solution_count, mi_ranked.size());
    result.solutions.reserve(num_solutions);

    // Precompute class-MI vector (shared across all exhaustive solutions)
    std::vector<double> mutual_informations(data.num_attributes(), 0.0);
    std::forward_list<std::size_t> all_useful;
    for (auto const &pair : mi_ranked) {
      mutual_informations[pair.second] = pair.first;
      all_useful.push_front(pair.second);
    }
    all_useful.reverse();
    mutual_informations[class_attribute_index] = -std::numeric_limits<double>::infinity();

    for (std::size_t s = 0; s < num_solutions; ++s) {
      std::size_t seed_attr = mi_ranked[s].second;

      mrmre_solution sol;
      sol.selected_indices.push_back(seed_attr);
      sol.scores.push_back(mutual_informations[seed_attr]);

      if (feature_count > 1 && mi_ranked.size() > 1) {
        // Build unselected list (all useful except the seed)
        std::forward_list<std::size_t> unselected = all_useful;
        unselected.remove(seed_attr);
        std::vector<double> redundance(data.num_attributes(), 0.0);

        mrmr_selection_loop(
            mutual_informations, redundance, unselected, seed_attr, 2,
            [&data](std::size_t a1, std::size_t a2) { return data.mutual_information(a1, a2); },
            [&sol, &data, &mutual_informations,
             feature_count](std::size_t /*rank*/, std::size_t attr_index, double score) {
              if (sol.selected_indices.size() < feature_count) {
                sol.selected_indices.push_back(attr_index);
                sol.scores.push_back(score);
              }
            });
      }

      result.solutions.push_back(std::move(sol));
    }

  } else if (method == mrmre_method::BOOTSTRAP) {
    std::mt19937 gen(seed);
    result.solutions.reserve(solution_count);

    for (std::size_t s = 0; s < solution_count; ++s) {
      // Create bootstrap view and run mRMR on it
      auto view = dataset_view<T>::bootstrap(data, gen);
      auto mrmr_result = mrmr(view, class_attribute_index, nullptr, cache_threshold);
      result.solutions.push_back(extract_solution(mrmr_result, feature_count));
    }
  }

  // Build consensus ranking
  auto consensus = build_consensus(result.solutions, data.num_attributes());
  result.consensus_ranking = std::move(consensus.first);
  result.feature_frequencies = std::move(consensus.second);

  return result;
}

#endif
