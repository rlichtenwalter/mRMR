// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2018-2026 Ryan N. Lichtenwalter

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

// ============================================================================
// KSG efficiency design rationale
// ============================================================================
//
// In mRMR feature selection, ksg_mi() is called O(M^2) times across M attribute
// pairs. Three optimizations reduce per-call overhead:
//
// 1. POINTS_ORIG ELIMINATION
//    The original code copied the 2D points array before make_kdtree rearranged
//    it, solely to preserve original coordinates for k-NN queries. Since the
//    input x[] and y[] arrays are never modified by make_kdtree, we construct
//    query points as point2d(x[i], y[i]) on the fly.
//    Saves: N * 16 bytes allocation + O(N) copy per call.
//
// 2. ALLOCATION REUSE (leaked thread_local scratch buffers)
//    Per-call vector allocations (points, sorted arrays, dists) are replaced
//    with intentionally-leaked thread_local buffers, following the established
//    pattern in compute_mi() (mi_policy.hpp). After warmup, subsequent calls
//    reuse existing allocations via resize(). This eliminates ~48N bytes of
//    malloc/free churn per ksg_mi call and ~(m-1) bytes per instance in
//    ross_mixed_mi. See mi_policy.hpp for the pattern rationale (static
//    destruction order, Google C++ Style Guide, Abseil NoDestructor).
//    Saves: Gigabytes of allocation churn across M^2 MI calls.
//
// 3. SINGLE-ENTRY SORTED MARGINAL CACHE
//    Each MI call requires sorted copies of both input columns for binary-search
//    neighbor counting. In the triangular MI cache loop, the outer column is
//    fixed across ~M inner iterations, causing it to be re-sorted M times
//    redundantly. A single-entry thread_local cache keyed on (pointer, size)
//    captures this reuse pattern automatically.
//
//    Alternatives considered and rejected:
//    - Pre-sorting all M columns: O(M*N) memory, unaffordable for large
//      datasets (e.g., 80 GB for M=10K, N=1M). Saves ~16% of total MI time.
//    - Tiling at block size T: O(T*N) memory, reduces total sorts by factor T.
//      Diminishing returns because kd-tree build+search (~84% of cost) is
//      unaffected by sort caching.
//    - Single-entry cache (T=1): O(N) memory (~8 MB for N=1M), captures the
//      dominant reuse pattern (outer-loop column). Saves ~8% of total MI time.
//      Best ratio of benefit to complexity.
//
//    The kd-tree build + k-NN searches constitute ~84% of per-call cost and
//    are inherently pair-specific (the 2D joint space changes for every attribute
//    pair — no caching possible). Sort caching targets the remaining ~16%.
//
//    The cache relies on stable column pointers from the calling dataset.
//    continuous_dataset passes &_data[attr * N] directly (zero-copy for
//    FloatT==double). mixed_dataset passes _continuous_cols[idx].data().
//    Both are stable for the dataset's lifetime.
//
//    Callers may also provide pre-sorted arrays via the x_sorted/y_sorted
//    parameters, bypassing both the cache lookup and internal sort.
// ============================================================================

namespace detail {

/**
 * @brief Single-entry thread_local sort cache for one marginal column.
 *
 * Keyed on (pointer, size) to detect when the same column is passed
 * across consecutive ksg_mi/ross_mixed_mi calls. The cache entry is
 * intentionally leaked (allocated via new, never freed) to avoid static
 * destruction order issues with thread_local in header-only templates.
 */
struct sorted_marginal_cache {
  double const *key = nullptr;
  std::size_t n = 0;
  std::vector<double> *sorted = new std::vector<double>();

  double const *get_or_sort(double const *col, std::size_t col_n, double const *provided) {
    if (provided) {
      return provided;
    }
    if (key == col && n == col_n) {
      return sorted->data();
    }
    sorted->assign(col, col + col_n);
    std::sort(sorted->begin(), sorted->end());
    key = col;
    n = col_n;
    return sorted->data();
  }
};

} // namespace detail

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
 * @param x          First variable values (N elements).
 * @param y          Second variable values (N elements).
 * @param n          Number of data points.
 * @param k          Number of nearest neighbors (default 6).
 * @param x_sorted   Optional pre-sorted copy of x (nullptr = sort internally).
 * @param y_sorted   Optional pre-sorted copy of y (nullptr = sort internally).
 * @return Estimated mutual information in bits.
 */
inline double ksg_mi(double const *x, double const *y, std::size_t n, std::size_t k = 6,
                     double const *x_sorted = nullptr, double const *y_sorted = nullptr) {
  if (k == 0 || n <= k + 1) {
    return 0.0; // insufficient data or degenerate k
  }

  // --- Reusable scratch buffer for 2D kd-tree points (leaked thread_local) ---
  using point2d = kdtree::point<double, 2>;
  static thread_local auto *points_ptr = new std::vector<point2d>();
  auto &points = *points_ptr;

  // --- Single-entry sort caches for each marginal dimension ---
  static thread_local detail::sorted_marginal_cache x_cache, y_cache;

  // Build 2D points for joint space kd-tree
  points.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    points[i] = point2d(x[i], y[i]);
  }

  // Build kd-tree (rearranges points in-place; x[] and y[] are unmodified)
  kdtree::make_kdtree(points.begin(), points.end());

  // Get sorted marginals (from cache, caller-provided, or fresh sort)
  double const *xs = x_cache.get_or_sort(x, n, x_sorted);
  double const *ys = y_cache.get_or_sort(y, n, y_sorted);

  // For each point, find k-th nearest neighbor distance (Chebyshev) and count marginals
  double sum_digamma = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    // Construct query point from original coordinates. x[] and y[] are never
    // modified by make_kdtree (it only rearranges the points array), so
    // point2d(x[i], y[i]) is equivalent to the former points_orig[i].
    point2d query(x[i], y[i]);

    // Search for k+1 neighbors because the query point exists in the tree
    // (it is one of the N data points) and will always be found as a neighbor
    // at distance 0 (self-match). Requesting k+1 ensures we get k actual
    // non-self neighbors. Epsilon (the max distance) is unaffected by the
    // self-match at distance 0 — it equals the k-th actual neighbor distance.
    auto neighbors = kdtree::nnsearch_kdtree<kdtree::chebyshev_metric>(points.begin(), points.end(),
                                                                       query, k + 1);

    // k-th actual neighbor Chebyshev distance (epsilon_i).
    // The max over k+1 results (including self at distance 0) gives the
    // distance to the k-th non-self neighbor.
    double epsilon = 0.0;
    for (auto const &nb : neighbors) {
      double d = kdtree::chebyshev_distance(*nb, query);
      if (d > epsilon) {
        epsilon = d;
      }
    }

    // Floor epsilon for degenerate cases where all k+1 neighbors coincide.
    if (epsilon == 0.0) {
      epsilon = std::numeric_limits<double>::epsilon();
    }

    // Count marginal neighbors within epsilon
    // n_x = |{j != i : |x_j - x_i| <= epsilon}| - 1 (exclude self)
    double xi = x[i], yi = y[i];
    auto x_lo = std::lower_bound(xs, xs + n, xi - epsilon);
    auto x_hi = std::upper_bound(xs, xs + n, xi + epsilon);
    // Use signed arithmetic to safely exclude self; clamp to 1 minimum
    auto raw_nx = static_cast<std::ptrdiff_t>(x_hi - x_lo) - 1;
    std::size_t n_x = (raw_nx > 0) ? static_cast<std::size_t>(raw_nx) : 1;

    auto y_lo = std::lower_bound(ys, ys + n, yi - epsilon);
    auto y_hi = std::upper_bound(ys, ys + n, yi + epsilon);
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
 * @param discrete           Discrete variable values (N elements, unsigned char).
 * @param continuous          Continuous variable values (N elements).
 * @param n                  Number of data points.
 * @param k                  Number of nearest neighbors (default 6).
 * @param continuous_sorted  Optional pre-sorted copy of continuous (nullptr = sort internally).
 * @return Estimated mutual information in bits.
 */
inline double ross_mixed_mi(unsigned char const *discrete, double const *continuous, std::size_t n,
                            std::size_t k = 6, double const *continuous_sorted = nullptr) {
  if (k == 0 || n <= k + 1) {
    return 0.0;
  }

  // Group instances by discrete label. Leaked thread_local avoids
  // reconstructing 256 inner vectors on every call; after warmup the
  // inner vectors retain capacity from prior calls.
  static thread_local auto *groups_ptr = new std::vector<std::vector<std::size_t>>(256);
  auto &groups = *groups_ptr;
  for (auto &g : groups) {
    g.clear();
  }
  for (std::size_t i = 0; i < n; ++i) {
    groups[discrete[i]].push_back(i);
  }

  // Get sorted continuous marginal (from cache, caller-provided, or fresh sort)
  static thread_local detail::sorted_marginal_cache cont_cache;
  double const *ys = cont_cache.get_or_sort(continuous, n, continuous_sorted);

  // Reusable scratch buffer for per-instance distance computation
  static thread_local auto *dists_ptr = new std::vector<double>();
  auto &dists = *dists_ptr;

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
    dists.clear();
    dists.reserve(m - 1);
    for (auto j : group) {
      if (j != i) {
        dists.push_back(std::abs(continuous[j] - continuous[i]));
      }
    }
    std::nth_element(dists.begin(), dists.begin() + static_cast<std::ptrdiff_t>(k) - 1,
                     dists.end());
    double epsilon = dists[k - 1];

    if (epsilon == 0.0) {
      epsilon = std::numeric_limits<double>::epsilon();
    }

    // Count ALL points within epsilon in the continuous dimension
    double yi = continuous[i];
    auto lo = std::lower_bound(ys, ys + n, yi - epsilon);
    auto hi = std::upper_bound(ys, ys + n, yi + epsilon);
    auto raw_nx = static_cast<std::ptrdiff_t>(hi - lo) - 1;
    std::size_t n_x = (raw_nx > 0) ? static_cast<std::size_t>(raw_nx) : 1;

    sum_digamma_nx += digamma(static_cast<double>(n_x));
    sum_digamma_m += digamma(static_cast<double>(m));
  }

  double mi_nats = digamma(static_cast<double>(k)) - sum_digamma_nx / static_cast<double>(n) +
                   digamma(static_cast<double>(n)) - sum_digamma_m / static_cast<double>(n);

  return std::max(0.0, mi_nats / std::log(2.0));
}

#endif // MRMR_HAS_CONTINUOUS

#endif // MRMR_KSG_ESTIMATOR_HPP
