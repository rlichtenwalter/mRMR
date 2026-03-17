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
#include <cmath>
#include <forward_list>
#include <functional>
#include <limits>
#include <mrmr/dataset.hpp>
#include <string>
#include <tuple>
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

template <typename T>
mrmr_return_type mrmr(dataset<T> const &data, std::size_t class_attribute_index,
                      mrmr_rank_callback on_rank = nullptr) {

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

  // Compute mRMR prerequisites
  std::vector<double> mutual_informations(data.num_attributes());
  std::vector<double> redundance(data.num_attributes(), 0.0);
  std::forward_list<std::size_t> unselected;
  std::vector<std::size_t> useless;
  for (std::size_t i = 0; i < data.num_attributes(); ++i) {
    if (i != class_attribute_index) {
      if (data.attribute_entropy(i) > 0) {
        mutual_informations[i] = data.mutual_information(class_attribute_index, i);
        unselected.push_front(i);
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

  // Handle special case of first attribute with highest mutual information.
  // Note: when all non-class attributes have zero MI with the class (degenerate case),
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

  // Main mRMR computation loop
  std::size_t rank = 2;
  while (!unselected.empty()) {
    double best_mrmr_score = -std::numeric_limits<double>::infinity();
    auto it = std::cbegin(unselected);
    auto last_it = unselected.before_begin();
    auto erase_it = last_it;
    while (it != std::cend(unselected)) {
      std::size_t attribute_index = *it;
      redundance.at(attribute_index) +=
          data.mutual_information(last_attribute_index, attribute_index);
      mrmr_score =
          mutual_informations.at(attribute_index) - redundance.at(attribute_index) / (rank - 1);
      if (mrmr_score - best_mrmr_score > std::numeric_limits<double>::epsilon()) {
        best_mrmr_score = mrmr_score;
        best_attribute_index = attribute_index;
        erase_it = last_it;
      }
      ++it;
      ++last_it;
    }

    emit_rank(rank++, best_attribute_index, data.attribute_name(best_attribute_index),
              data.attribute_entropy(best_attribute_index),
              mutual_informations.at(best_attribute_index), best_mrmr_score);

    unselected.erase_after(erase_it);
    last_attribute_index = best_attribute_index;
  }

  // Append useless features (zero-entropy attributes)
  std::sort(useless.begin(), useless.end());
  for (auto attribute_index : useless) {
    emit_rank(rank++, attribute_index, data.attribute_name(attribute_index), 0, 0,
              -std::numeric_limits<double>::infinity());
  }

  return retval;
}

#endif
