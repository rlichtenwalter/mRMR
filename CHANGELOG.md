# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Gitea Actions workflow that mirrors Gitea releases to GitHub, closing the push mirror's release-metadata gap
  - Manual backfill against any existing release via `workflow_dispatch` with a `tag` input

### Changed
- Standards alignment: the `mixed-line-ending` pre-commit hook now forces LF, and `.gitignore` covers `.env` secrets files

## [2.0.0] - 2026-04-27

### Added
- `check-json` pre-commit hook validating `CMakePresets.json` and any future JSON files at commit time
- Sibling-alignment cleanup matching the conventions already in `vcp` and
  `kdtree`:
  - **C++ standard bumped to C++20** (`cxx_std_14` → `cxx_std_20`); no source changes forced
  - `.clang-tidy` suppresses `performance-enum-size`, `misc-use-internal-linkage`, and `modernize-concat-nested-namespaces`, matching `vcp`
  - C++20 idioms adopted across library, CLI, tests, and benchmarks (ranges, `std::numbers`, designated initializers, `std::seed_seq` seeding) to satisfy the active clang-tidy checks
  - CI `lint` job now runs `clang-tidy` on `tools/*.cpp` and `test/*.cpp` sources only; header diagnostics propagate via `HeaderFilterRegex`
  - CI `lint` job dropped the redundant Python setup and standalone formatting step
  - CI job order normalized to `build-and-test` → `quality` → `lint` → `sanitize`, matching `vcp`/`kdtree`
  - Pre-commit hooks declare `stages: [pre-commit]` explicitly per hook
  - Release compile flags include `-DNDEBUG` explicitly, matching the vcp pattern
  - `.gitignore` entries reordered to alphabetical, matching `vcp` and `kdtree`
- Branch protection hook (no-commit-to-branch) for main and develop
- Detect-private-key pre-commit hook
- New CI `sanitize` job that builds Debug with `MRMR_SANITIZE=ON` and runs the full ctest suite under ASan+UBSan on every PR.
- `CMakePresets.json` with `release`, `debug`, and `sanitize` configure/build/test presets, each with its own `build/<name>` tree so switching configs keeps a warm cache

### Changed
- **Relicensed from GPL-3.0 to BSD-3-Clause**, matching the `vcp` and `kdtree` sibling repositories
  - Per-file GPL boilerplate replaced with `SPDX-License-Identifier` headers on every source file
- **API surface (technically breaking but unadvertised):** moved `delimiter_ctype.hpp` from `include/mrmr/` to `include/mrmr/detail/`
  - Downstreams including `<mrmr/delimiter_ctype.hpp>` directly must update the path
- CI `build-and-test` job extended with a Clang matrix entry; GCC and Clang both build everything and run the full ctest suite at Release and Debug
- Test and benchmark targets now compile with the same warning flags as the CLI tool, via a new shared `MRMR_WARNING_FLAGS` CMake variable
- `MRMR_WARNING_FLAGS` expanded with `-Wconversion`, `-Wsign-conversion`, `-Wshadow`, and other stronger checks, with mechanical cast fixes across library and benchmarks
- Catch2 headers are now treated as system includes, so warnings from Catch2's own code no longer break `-Werror` builds
- **BREAKING**: CMake minimum requirement raised from 3.21 to 3.24; all current target distros ship CMake >= 3.24 in their default repositories
- `.gitea/workflows/ci.yml` now invokes CMake presets instead of inline configure flags; sanitizer runtime options live on the `sanitize` test preset, not the workflow yaml
- `CMAKE_EXPORT_COMPILE_COMMANDS` is now set unconditionally in `CMakeLists.txt`, so editor LSPs and the `lint` CI step both find a compilation database without per-invocation flags
- `.gitignore` simplified: the `build-*/` glob is removed in favor of
  the existing `build/` rule, since presets place all per-config trees
  under `build/<name>/`.
- `MRMR_SANITIZE` now enables ASan **and** UBSan on every built target (previously ASan on the CLI only), with every sanitizer diagnostic treated as a hard error
- Update clang-format to v22.1.2 for fleet-wide consistency

### Fixed
- CI clang-tidy compile database generation and bugprone-branch-clone false positive on option-parsing chains
- Guard continuous-only CLI variables with MRMR_HAS_CONTINUOUS to prevent unused-variable warnings without -DMRMR_CONTINUOUS=ON
- Resolve all clang-tidy warnings across headers, CLI, and tests; align CI lint config with local .clang-tidy
- Skip the `no-commit-to-branch` pre-commit hook in CI, where it fired spuriously on checkouts of `main`/`develop`

## [1.0.0] - 2026-03-18

### Added
- CMake build system with install support (find_package and pkg-config)
- CTest integration with Catch2 v3 unit test suite (62 tests total)
- CLI integration tests covering file/stdin input, flags, error handling, and ensemble output
- clang-format and clang-tidy configurations
- pre-commit hooks for formatting and file hygiene
- Gitea Actions CI workflow (build Release/Debug matrix, test, lint)
- Optional callback parameter to mrmr() for streaming per-rank output
- static_assert constraining storage type T to max value <= 255
- Value compaction pass ensuring contiguous attribute indices after discretization
- Triangular MI cache for O(1) pairwise MI lookup with dynamic strategy selection
  based on attribute count (precompute for M <= 5000, on-the-fly for larger datasets)
- Doxygen-compatible docstrings (LLVM style) for all public API elements
- dataset_view for zero-copy bootstrap resampling with sorted-index indirection
- MI policy template: unweighted (integer histogram), weighted (double histogram),
  and pairwise-complete (skip missing pairs) policies with zero-overhead dispatch
- mRMRe ensemble feature selection: exhaustive (different seed features) and bootstrap
  (resample instances) methods with consensus ranking via feature frequency aggregation,
  generalized for all DataSource types
- Missing value support: sentinel (255 for unsigned char), imputation (mode, median, mean),
  and pairwise-complete MI computation
- Continuous dataset with KSG Algorithm 1 MI estimator (Kraskov et al., 2004) using
  Chebyshev distance kd-tree for joint space k-NN search
- Mixed dataset with type-segregated storage (discrete as unsigned char, continuous as
  double) and per-pair MI dispatch: histogram (DD), KSG (CC), Ross 2014 (DC/CD)
- CLI: --method={discrete,continuous} for dataset type selection, --missing={error,
  impute-mode,impute-median,impute-mean,pairwise} for missing value handling, --info
  for dataset summary, --ksg-k for KSG neighbor count, grouped help output
- Bootstrap resampling for all dataset types: zero-copy view for discrete, native-type
  column copying for continuous and mixed
- operator() cell accessor on mixed_dataset returning double
- Catch2 performance benchmarks for view access patterns, MI computation, and continuous data

### Changed
- BREAKING: Headers moved to include/mrmr/ subdirectory
- BREAKING: Replaced Makefile with CMake
- BREAKING: dataset constructor now takes delimiter parameter instead of using global variable
- BREAKING: mrmr() templated on DataSource concept instead of dataset storage type T
- CLI tool moved from src/ to tools/
- CLI version string read from VERSION file via CMake
- CLI uses library mrmr() function with callback instead of inline algorithm
- Normalized include guard naming convention (MRMR_ prefix)
- All source files formatted with clang-format (LLVM style, 100 column limit)
- Restructure discretization pipeline: compute min/max first, then translate and compact
- MI computation uses leaked thread_local scratch buffers for zero-allocation reuse
  (following Google C++ Style Guide / Abseil NoDestructor pattern)
- KSG MI estimator: single-entry sorted marginal cache capturing outer-loop column reuse,
  optional pre-sorted array parameters, zero-copy column access for FloatT==double
- Matrix parser uses vector for dynamic growth, throws exceptions instead of exit()
- Matrix I/O uses member delimiter instead of global variable

### Fixed
- Fix delimiter_ctype iterate-by-value bug that failed to clear previous space bits
- Preserve newline as whitespace in custom delimiter locale for correct header parsing
- Fix histogram array off-by-one (max() → max() + 1) in attribute_information
- Fix non-contiguous attribute value handling that caused buffer overruns in mutual_information
- Fix matrix parser EOF handling (replace while(!is.eof()) with read-then-check pattern)
- Eliminate global mutable DELIMITER state; delimiter is now per-dataset instance
- Eliminate duplicated mRMR algorithm in CLI (was diverging from library implementation)
- Fix discretization overflow guard boundary and NaN/Inf handling with tag dispatch
- Fix weighted_policy::normalize to divide by total weight instead of returning raw value
- Fix pairwise_complete_policy to derive marginals from joint histogram
- Fix impute_mean sentinel collision by clamping to [0, sentinel-1]
- Fix dataset_view MI to use source dataset's attribute_information for histogram sizing
- Fix mrmr first-rank selection to search only useful attributes
- Fix KSG self-inclusion: search k+1 neighbors since query point is always found as
  self-match at distance 0 in the kd-tree
- Guard against k=0 in ksg_mi and ross_mixed_mi to prevent undefined behavior
- Fix compute_variation undefined behavior when num_instances is zero

## [0.9.3] - 2020-12-07

### Added
- Header for easier usage as library, principally in support of Python bindings

## [0.9.2]

### Added
- Example data and updated README with example usage
- Note about expected feature value ranges

## [0.9.1]

### Added
- Support for new warning log level (now the default)
- Attribute domain translation and integer overflow detection

### Changed
- Refactored discretization code
- Add 'truncate' discretization procedure (now the default)
- Warn when discretization method is not explicitly chosen

## [0.9.0]

### Added
- Delimiter specification support

### Changed
- Minor improvement to log handling
- Small incidental code changes

## [0.1.0]

### Added
- Initial release
