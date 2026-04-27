// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2018-2026 Ryan N. Lichtenwalter

#ifndef MRMR_MISSING_HPP
#define MRMR_MISSING_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

/**
 * @brief Sentinel value indicating a missing discrete observation.
 *
 * For unsigned char storage, value 255 is reserved as the missing sentinel.
 * This reduces the representable value range to [0, 254] (255 distinct values).
 * All MI computation and imputation routines use this constant to identify
 * missing data.
 */
template <typename T> struct missing_sentinel {
  static constexpr T value = std::numeric_limits<T>::max();
};

/**
 * @brief Check whether a value represents a missing observation.
 */
template <typename T> bool is_missing(T val) { return val == missing_sentinel<T>::value; }

/**
 * @brief Strategy for handling missing values.
 */
enum class missing_strategy : std::uint8_t {
  ERROR,         ///< Throw an exception if any missing values are found.
  PAIRWISE,      ///< Use pairwise-complete observations in MI computation.
  IMPUTE_MODE,   ///< Replace missing with the most frequent value per attribute.
  IMPUTE_MEDIAN, ///< Replace missing with the median value per attribute.
  IMPUTE_MEAN    ///< Replace missing with the mean value (rounded for discrete).
};

/**
 * @brief Count missing values per attribute in a column-major data matrix.
 *
 * @tparam T Value type (typically unsigned char).
 * @param data         Column-major data: data[attr * num_instances + inst].
 * @param num_attrs    Number of attributes.
 * @param num_insts    Number of instances.
 * @return Vector of missing counts per attribute.
 */
template <typename T>
std::vector<std::size_t> count_missing(T const *data, std::size_t num_attrs,
                                       std::size_t num_insts) {
  std::vector<std::size_t> counts(num_attrs, 0);
  for (std::size_t attr = 0; attr < num_attrs; ++attr) {
    for (std::size_t inst = 0; inst < num_insts; ++inst) {
      if (is_missing(data[attr * num_insts + inst])) {
        ++counts[attr];
      }
    }
  }
  return counts;
}

/**
 * @brief Validate that no missing values exist. Throws if any are found.
 *
 * @tparam T Value type.
 * @param data         Column-major data.
 * @param num_attrs    Number of attributes.
 * @param num_insts    Number of instances.
 * @param attr_names   Attribute names for error messages.
 * @throws std::runtime_error If any missing values are found.
 */
template <typename T>
void validate_no_missing(T const *data, std::size_t num_attrs, std::size_t num_insts,
                         std::vector<std::string> const &attr_names) {
  auto counts = count_missing(data, num_attrs, num_insts);
  for (std::size_t attr = 0; attr < num_attrs; ++attr) {
    if (counts[attr] > 0) {
      throw std::runtime_error("attribute '" + attr_names[attr] + "' has " +
                               std::to_string(counts[attr]) + " missing value(s)");
    }
  }
}

/**
 * @brief Impute missing values with the mode (most frequent value) per attribute.
 *
 * If all instances of an attribute are missing, they are left unchanged
 * (sentinel preserved) since no observed values exist to derive a mode from.
 *
 * @tparam T Value type (must be unsigned integer with max <= 255).
 * @param data         Column-major data (modified in place).
 * @param num_attrs    Number of attributes.
 * @param num_insts    Number of instances.
 */
template <typename T> void impute_mode(T *data, std::size_t num_attrs, std::size_t num_insts) {
  for (std::size_t attr = 0; attr < num_attrs; ++attr) {
    // Build histogram excluding missing values
    std::vector<std::size_t> histogram(missing_sentinel<T>::value, 0);
    for (std::size_t inst = 0; inst < num_insts; ++inst) {
      T val = data[attr * num_insts + inst];
      if (!is_missing(val)) {
        ++histogram[val];
      }
    }

    // Find mode; skip if all values are missing (no observed data to derive mode from)
    T mode_val = 0;
    std::size_t mode_count = 0;
    for (std::size_t v = 0; v < histogram.size(); ++v) {
      if (histogram[v] > mode_count) {
        mode_count = histogram[v];
        mode_val = static_cast<T>(v);
      }
    }

    if (mode_count == 0) {
      continue; // all missing — nothing to impute from
    }

    // Replace missing with mode
    for (std::size_t inst = 0; inst < num_insts; ++inst) {
      if (is_missing(data[attr * num_insts + inst])) {
        data[attr * num_insts + inst] = mode_val;
      }
    }
  }
}

/**
 * @brief Impute missing values with the median value per attribute.
 *
 * Uses the lower-middle element for even-length sequences (standard convention
 * for integer types where averaging two values may lose information).
 * If all instances are missing, they are left unchanged.
 *
 * @tparam T Value type.
 * @param data         Column-major data (modified in place).
 * @param num_attrs    Number of attributes.
 * @param num_insts    Number of instances.
 */
template <typename T> void impute_median(T *data, std::size_t num_attrs, std::size_t num_insts) {
  for (std::size_t attr = 0; attr < num_attrs; ++attr) {
    // Collect non-missing values
    std::vector<T> values;
    values.reserve(num_insts);
    for (std::size_t inst = 0; inst < num_insts; ++inst) {
      T val = data[attr * num_insts + inst];
      if (!is_missing(val)) {
        values.push_back(val);
      }
    }

    if (values.empty()) {
      continue; // all missing, nothing to impute from
    }

    // Find lower median (conventional for integer types)
    std::sort(values.begin(), values.end());
    T median_val = values[(values.size() - 1) / 2];

    // Replace missing with median
    for (std::size_t inst = 0; inst < num_insts; ++inst) {
      if (is_missing(data[attr * num_insts + inst])) {
        data[attr * num_insts + inst] = median_val;
      }
    }
  }
}

/**
 * @brief Impute missing values with the mean value per attribute (rounded for integer types).
 *
 * The result is clamped to [0, sentinel - 1] to prevent the imputed value from
 * colliding with the missing sentinel. If all instances are missing, they are
 * left unchanged.
 *
 * @tparam T Value type.
 * @param data         Column-major data (modified in place).
 * @param num_attrs    Number of attributes.
 * @param num_insts    Number of instances.
 */
template <typename T> void impute_mean(T *data, std::size_t num_attrs, std::size_t num_insts) {
  for (std::size_t attr = 0; attr < num_attrs; ++attr) {
    double sum = 0;
    std::size_t count = 0;
    for (std::size_t inst = 0; inst < num_insts; ++inst) {
      T val = data[attr * num_insts + inst];
      if (!is_missing(val)) {
        sum += static_cast<double>(val);
        ++count;
      }
    }

    if (count == 0) {
      continue;
    }

    // Round to nearest integer, clamp to [0, sentinel - 1] to prevent sentinel collision
    double rounded = sum / static_cast<double>(count) + 0.5;
    if (rounded >= static_cast<double>(missing_sentinel<T>::value)) {
      rounded = static_cast<double>(missing_sentinel<T>::value) - 1.0;
    }
    T mean_val = static_cast<T>(rounded);

    for (std::size_t inst = 0; inst < num_insts; ++inst) {
      if (is_missing(data[attr * num_insts + inst])) {
        data[attr * num_insts + inst] = mean_val;
      }
    }
  }
}

#endif
