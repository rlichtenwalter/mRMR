# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Branch protection hook (no-commit-to-branch) for main and develop

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
