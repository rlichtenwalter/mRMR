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

#ifndef MRMR_KSG_ESTIMATOR_HPP
#define MRMR_KSG_ESTIMATOR_HPP

#ifdef MRMR_HAS_CONTINUOUS

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <kdtree/kdtree.hpp>
#include <kdtree/point.hpp>
#include <limits>
#include <vector>

/**
 * @brief Digamma (psi) function — the logarithmic derivative of the gamma function.
 *
 * Uses the asymptotic expansion for argument >= 6, with recurrence relation
 * for smaller arguments. Accurate to machine precision for all positive arguments.
 * Required by the KSG mutual information estimator.
 *
 * @param x Positive argument.
 * @return psi(x) = d/dx ln(Gamma(x)).
 */
inline double digamma(double x) {
  // Recurrence: psi(x) = psi(x+1) - 1/x
  double result = 0.0;
  while (x < 6.0) {
    result -= 1.0 / x;
    x += 1.0;
  }
  // Asymptotic expansion for large x:
  // psi(x) ~ ln(x) - 1/(2x) - 1/(12x^2) + 1/(120x^4) - 1/(252x^6)
  double inv_x = 1.0 / x;
  double inv_x2 = inv_x * inv_x;
  result +=
      std::log(x) - 0.5 * inv_x - inv_x2 * (1.0 / 12.0 - inv_x2 * (1.0 / 120.0 - inv_x2 / 252.0));
  return result;
}

/**
 * @brief Compute mutual information between two continuous variables using
 *        KSG Algorithm 1 (Kraskov, Stoegbauer, Grassberger, 2004).
 *
 * For each point, finds the k-th nearest neighbor in the 2D joint space
 * using Chebyshev (max-norm) distance. Then counts neighbors within that
 * distance in each 1D marginal. The MI estimate is:
 *
 *   MI = psi(k) - <psi(n_x + 1) + psi(n_y + 1)> + psi(N)
 *
 * where n_x and n_y are the marginal neighbor counts and <.> denotes the
 * average over all N points.
 *
 * @param x      First variable values (N elements).
 * @param y      Second variable values (N elements).
 * @param n      Number of data points.
 * @param k      Number of nearest neighbors (default 6).
 * @return Estimated mutual information in nats. Divide by log(2) for bits.
 */
inline double ksg_mi(double const *x, double const *y, std::size_t n, std::size_t k = 6) {
  if (n <= k + 1) {
    return 0.0; // insufficient data
  }

  // Build 2D points for joint space kd-tree
  using point2d = kdtree::point<double, 2>;
  std::vector<point2d> points(n);
  for (std::size_t i = 0; i < n; ++i) {
    points[i] = point2d(x[i], y[i]);
  }

  // Keep original order for marginal lookups
  std::vector<point2d> points_orig = points;

  // Build kd-tree (in-place sort)
  kdtree::make_kdtree(points.begin(), points.end());

  // Sorted marginal copies for binary search counting
  std::vector<double> x_sorted(n), y_sorted(n);
  for (std::size_t i = 0; i < n; ++i) {
    x_sorted[i] = x[i];
    y_sorted[i] = y[i];
  }
  std::sort(x_sorted.begin(), x_sorted.end());
  std::sort(y_sorted.begin(), y_sorted.end());

  // For each point, find k-th nearest neighbor distance (Chebyshev) and count marginals
  double sum_digamma = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    // kNN search using Chebyshev metric
    // Template params: Metric only; LeafThreshold and Iterator/Point are deduced
    auto neighbors = kdtree::nnsearch_kdtree<kdtree::chebyshev_metric>(points.begin(), points.end(),
                                                                       points_orig[i], k);

    // k-th neighbor Chebyshev distance (epsilon_i)
    double epsilon = 0.0;
    for (auto const &nb : neighbors) {
      double d = kdtree::chebyshev_distance(*nb, points_orig[i]);
      if (d > epsilon) {
        epsilon = d;
      }
    }

    // Add small noise to break ties (standard practice, cf. scikit-learn)
    if (epsilon == 0.0) {
      epsilon = std::numeric_limits<double>::epsilon();
    }

    // Count marginal neighbors within epsilon (exclusive)
    // n_x = |{j != i : |x_j - x_i| < epsilon}|
    double xi = x[i], yi = y[i];
    auto x_lo = std::lower_bound(x_sorted.begin(), x_sorted.end(), xi - epsilon);
    auto x_hi = std::upper_bound(x_sorted.begin(), x_sorted.end(), xi + epsilon);
    // Use signed arithmetic to safely exclude self; clamp to 1 minimum
    auto raw_nx = static_cast<std::ptrdiff_t>(x_hi - x_lo) - 1;
    std::size_t n_x = (raw_nx > 0) ? static_cast<std::size_t>(raw_nx) : 1;

    auto y_lo = std::lower_bound(y_sorted.begin(), y_sorted.end(), yi - epsilon);
    auto y_hi = std::upper_bound(y_sorted.begin(), y_sorted.end(), yi + epsilon);
    auto raw_ny = static_cast<std::ptrdiff_t>(y_hi - y_lo) - 1;
    std::size_t n_y = (raw_ny > 0) ? static_cast<std::size_t>(raw_ny) : 1;

    sum_digamma +=
        digamma(static_cast<double>(n_x) + 1.0) + digamma(static_cast<double>(n_y) + 1.0);
  }

  // KSG Algorithm 1 formula
  double mi_nats = digamma(static_cast<double>(k)) - sum_digamma / static_cast<double>(n) +
                   digamma(static_cast<double>(n));

  // Convert from nats to bits
  return std::max(0.0, mi_nats / std::log(2.0));
}

/**
 * @brief Compute MI between a discrete and a continuous variable using
 *        the Ross (2014) conditional entropy estimator.
 *
 * MI(X_discrete; Y_continuous) = psi(k) - <psi(n_label)> + psi(N)
 * where for each point i, we find the k-th nearest neighbor among points
 * sharing the same discrete label, compute epsilon_i, then count how many
 * of ALL points fall within epsilon_i in the continuous dimension.
 *
 * @param discrete  Discrete variable values (N elements, unsigned char).
 * @param continuous Continuous variable values (N elements).
 * @param n         Number of data points.
 * @param k         Number of nearest neighbors (default 6).
 * @return Estimated mutual information in bits.
 */
inline double ross_mixed_mi(unsigned char const *discrete, double const *continuous, std::size_t n,
                            std::size_t k = 6) {
  if (n <= k + 1) {
    return 0.0;
  }

  // Group instances by discrete label
  std::vector<std::vector<std::size_t>> groups(256);
  for (std::size_t i = 0; i < n; ++i) {
    groups[discrete[i]].push_back(i);
  }

  // Sorted continuous values for binary search
  std::vector<double> y_sorted(continuous, continuous + n);
  std::sort(y_sorted.begin(), y_sorted.end());

  double sum_digamma_nx = 0.0;
  double sum_digamma_m = 0.0;

  for (std::size_t i = 0; i < n; ++i) {
    unsigned char label = discrete[i];
    auto const &group = groups[label];
    std::size_t m = group.size();

    if (m <= k) {
      // Not enough points in this class for k neighbors — skip
      // (contributes psi(m) for this label)
      sum_digamma_m += digamma(static_cast<double>(m));
      sum_digamma_nx += digamma(1.0); // minimal neighbor count
      continue;
    }

    // Find k-th nearest neighbor within same label in 1D continuous space
    std::vector<double> dists;
    dists.reserve(m - 1);
    for (auto j : group) {
      if (j != i) {
        dists.push_back(std::abs(continuous[j] - continuous[i]));
      }
    }
    std::nth_element(dists.begin(), dists.begin() + static_cast<long>(k) - 1, dists.end());
    double epsilon = dists[k - 1];

    if (epsilon == 0.0) {
      epsilon = std::numeric_limits<double>::epsilon();
    }

    // Count ALL points within epsilon in the continuous dimension
    double yi = continuous[i];
    auto lo = std::lower_bound(y_sorted.begin(), y_sorted.end(), yi - epsilon);
    auto hi = std::upper_bound(y_sorted.begin(), y_sorted.end(), yi + epsilon);
    auto raw_nx = static_cast<std::ptrdiff_t>(hi - lo) - 1;
    std::size_t n_x = (raw_nx > 0) ? static_cast<std::size_t>(raw_nx) : 1;
    if (n_x == 0) {
      n_x = 1;
    }

    sum_digamma_nx += digamma(static_cast<double>(n_x));
    sum_digamma_m += digamma(static_cast<double>(m));
  }

  double mi_nats = digamma(static_cast<double>(k)) - sum_digamma_nx / static_cast<double>(n) +
                   digamma(static_cast<double>(n)) - sum_digamma_m / static_cast<double>(n);

  return std::max(0.0, mi_nats / std::log(2.0));
}

#endif // MRMR_HAS_CONTINUOUS

#endif // MRMR_KSG_ESTIMATOR_HPP
