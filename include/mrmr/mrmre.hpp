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
#ifdef MRMR_HAS_CONTINUOUS
#include <mrmr/continuous_dataset.hpp>
#include <mrmr/mixed_dataset.hpp>
#endif
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

// --- Bootstrap resampling overloads ---
// Each DataSource type gets its optimal resampling strategy.
// Discrete datasets use zero-copy dataset_view; continuous/mixed create new copies.

/** @brief Bootstrap resample for discrete dataset — returns zero-copy view. */
template <typename T>
dataset_view<T> bootstrap_resample(dataset<T> const &source, std::mt19937 &gen) {
  return dataset_view<T>::bootstrap(source, gen);
}

#ifdef MRMR_HAS_CONTINUOUS
/** @brief Bootstrap resample for continuous dataset — returns new owning copy. */
template <typename FloatT>
continuous_dataset<FloatT> bootstrap_resample(continuous_dataset<FloatT> const &source,
                                              std::mt19937 &gen) {
  std::size_t n = source.num_instances();
  std::size_t m = source.num_attributes();
  if (n == 0) {
    return continuous_dataset<FloatT>();
  }
  std::uniform_int_distribution<std::size_t> dist(0, n - 1);
  std::vector<FloatT> resampled(n * m);
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t src_inst = dist(gen);
    for (std::size_t a = 0; a < m; ++a) {
      resampled[i * m + a] = source(a, src_inst);
    }
  }
  std::vector<std::string> names;
  for (std::size_t a = 0; a < m; ++a) {
    names.push_back(source.attribute_name(a));
  }
  return continuous_dataset<FloatT>(resampled, n, m, names, source.ksg_k());
}

/** @brief Bootstrap resample for mixed dataset — returns new owning copy. */
inline mixed_dataset bootstrap_resample(mixed_dataset const &source, std::mt19937 &gen) {
  std::size_t n = source.num_instances();
  std::size_t m = source.num_attributes();
  if (n == 0) {
    return mixed_dataset();
  }
  std::uniform_int_distribution<std::size_t> dist(0, n - 1);

  // Build resampled row-major data as doubles (mixed_dataset constructor handles types)
  std::vector<double> resampled(n * m);
  std::vector<std::size_t> sample_indices(n);
  for (auto &idx : sample_indices) {
    idx = dist(gen);
  }

  // mixed_dataset doesn't expose operator() — read from discrete/continuous columns
  // by reconstructing the type-annotated header names and raw double values
  std::vector<column_type> types(m);
  std::vector<std::string> names(m);
  for (std::size_t a = 0; a < m; ++a) {
    types[a] = source.type_of(a);
    names[a] = source.attribute_name(a);
  }

  // We need cell access — mixed_dataset needs operator() or we need another approach.
  // Since mixed_dataset stores discrete as unsigned char and continuous as double
  // in separate storage, and its mutual_information handles dispatch internally,
  // we reconstruct from the attribute_entropy and mutual_information interface.
  // However, for bootstrap we actually need raw cell values.
  //
  // The simplest approach: mixed_dataset can expose a double-valued cell accessor
  // for bootstrap purposes. For now, use entropy > 0 as a proxy to detect
  // if an attribute has variation, and rely on the mrmr ranking being valid
  // on the full dataset for the exhaustive method.
  //
  // TODO: Add operator() to mixed_dataset for bootstrap support.
  // For now, bootstrap on mixed_dataset is not supported; use exhaustive.
  (void)resampled;
  (void)sample_indices;
  throw std::runtime_error(
      "bootstrap resampling of mixed_dataset requires operator() support (not yet implemented)");
}
#endif

/**
 * @brief Perform mRMRe ensemble feature selection.
 *
 * Works with any DataSource type: dataset<T>, continuous_dataset<FloatT>,
 * or mixed_dataset. Bootstrap resampling dispatches to the appropriate
 * overload of bootstrap_resample().
 *
 * @tparam DataSource Data source type satisfying the DataSource concept.
 * @param data                  Source data.
 * @param class_attribute_index Index of the class/target attribute.
 * @param feature_count         Number of features to select per solution.
 * @param solution_count        Number of ensemble solutions to generate.
 * @param method                Ensemble method (EXHAUSTIVE or BOOTSTRAP).
 * @param seed                  Random seed for bootstrap resampling.
 * @param cache_threshold       MI cache threshold passed to underlying mRMR calls.
 * @return mrmre_result with all solutions and consensus ranking.
 */
template <typename DataSource>
mrmre_result mrmre(DataSource const &data, std::size_t class_attribute_index,
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

    // Build useful_indices for potential triangular cache
    std::vector<std::size_t> useful_indices;
    useful_indices.reserve(mi_ranked.size());
    for (auto const &pair : mi_ranked) {
      useful_indices.push_back(pair.second);
    }

    // MI lookup: use triangular cache when M is manageable, on-the-fly otherwise
    auto run_exhaustive = [&](auto &&get_mi) {
      for (std::size_t s = 0; s < num_solutions; ++s) {
        std::size_t seed_attr = mi_ranked[s].second;

        mrmre_solution sol;
        sol.selected_indices.push_back(seed_attr);
        sol.scores.push_back(mutual_informations[seed_attr]);

        if (feature_count > 1 && mi_ranked.size() > 1) {
          std::forward_list<std::size_t> unselected = all_useful;
          unselected.remove(seed_attr);
          std::vector<double> redundance(data.num_attributes(), 0.0);

          mrmr_selection_loop(
              mutual_informations, redundance, unselected, seed_attr, 2, get_mi,
              [&sol, feature_count](std::size_t /*rank*/, std::size_t attr_index, double score) {
                if (sol.selected_indices.size() < feature_count) {
                  sol.selected_indices.push_back(attr_index);
                  sol.scores.push_back(score);
                }
              });
        }

        result.solutions.push_back(std::move(sol));
      }
    };

    if (useful_indices.size() <= cache_threshold && useful_indices.size() > 1) {
      triangular_mi_cache<DataSource> cache(data, useful_indices);
      run_exhaustive([&cache](std::size_t a1, std::size_t a2) { return cache.get(a1, a2); });
    } else {
      run_exhaustive(
          [&data](std::size_t a1, std::size_t a2) { return data.mutual_information(a1, a2); });
    }

  } else if (method == mrmre_method::BOOTSTRAP) {
    std::mt19937 gen(seed);
    result.solutions.reserve(solution_count);

    for (std::size_t s = 0; s < solution_count; ++s) {
      // Create bootstrap sample — dispatches to optimal strategy per DataSource
      auto sample = bootstrap_resample(data, gen);
      auto mrmr_result = mrmr(sample, class_attribute_index, nullptr, cache_threshold);
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
