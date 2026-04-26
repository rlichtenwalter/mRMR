# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- `check-json` pre-commit hook (commit stage), validates `CMakePresets.json`
  and any future JSON files at commit time. Closes a small gap flagged by
  `/standards-check` (`precommit.check_json` warning).
- Sibling-alignment cleanup matching the conventions already in `vcp` and
  `kdtree`:
  - **C++ standard bumped to C++20** (`cxx_std_14` → `cxx_std_20` in
    `CMakeLists.txt`). C++14 was a valid subset of C++20, so no source
    changes were forced; the bump aligns the minimum standard with `vcp`
    and clears the way for opportunistic adoption of C++20 idioms
    (concepts, `std::span`, `std::bit_width`, etc.) at the next natural
    refactor point. README updated to reflect "C++20 compliant code base".
  - `.clang-tidy` suppresses `performance-enum-size` and
    `misc-use-internal-linkage`, matching `vcp`/`kdtree`. Both checks
    shipped with clang-tidy 20.x and were never propagated to mRMR.
  - CI `lint` job's `clang-tidy` invocation now also covers `test/*.cpp`
    so test code is held to the same lint contract as the library and
    CLI (matching `vcp` and `kdtree`).
  - CI `lint` job dropped the redundant `Setup Python` action, the
    `pip install pre-commit` line, and the standalone `Check formatting`
    step — clang-format is already enforced by the `quality` job's
    pre-commit run, and the lint job only needs `clang-tidy` installed.
    The result matches the `vcp`/`kdtree` lint job shape exactly.
  - CI job order normalized to `build-and-test` → `quality` → `lint` →
    `sanitize`, matching `vcp`/`kdtree`. Build success is the most
    fundamental signal; lint/sanitize feedback is uninteresting if the
    code does not compile, so the file reads top-down from "does this
    build" to progressively more specialized validations. Jobs all run
    in parallel anyway — this is purely about file readability.
  - Pre-commit hooks now declare `stages: [pre-commit]` explicitly per
    hook (in addition to the global `default_stages`), matching `vcp`
    and `kdtree`. Redundant with the default but makes intent obvious.
  - Release compile flags include `-DNDEBUG` explicitly, matching the
    vcp pattern. CMake's default `Release` config supplies `-DNDEBUG`
    on GCC/Clang already, so this is a no-op at compile time, but
    spelling it out alongside `-O3 -fomit-frame-pointer` keeps the
    intended Release contract visible in one place rather than split
    between project flags and CMake defaults.
  - `.gitignore` entries reordered to alphabetical (`build/`, `.cache/`,
    `.claude/`, `**/.vscode`, `.nfs*`), matching `vcp` and `kdtree`.
    Functionally equivalent (gitignore order is irrelevant) — purely
    cosmetic alignment.
- Branch protection hook (no-commit-to-branch) for main and develop
- Detect-private-key pre-commit hook
- New CI `sanitize` job that builds Debug with `MRMR_SANITIZE=ON` and runs the full ctest suite under ASan+UBSan on every PR.
- `CMakePresets.json` at the repository root with three named configurations
  (`release`, `debug`, `sanitize`) covering the meaningful build contexts
  the project ships. Each preset has its own `binaryDir` under `build/<name>`,
  so switching between configs no longer triggers a full rebuild — each tree
  keeps its own warm cache. Build presets and test presets mirror configure
  presets one-for-one; the `sanitize` test preset carries the
  `ASAN_OPTIONS` / `UBSAN_OPTIONS` halt-on-error contract that was
  previously duplicated inline in CI yaml. Preset file at version `3`
  (CMake 3.21+, well within the 3.24 floor); `cmakeMinimumRequired`
  declares 3.24 explicitly so older toolchains refuse to load it.
  IDEs that support presets (VSCode CMake Tools, CLion, KDevelop, Qt
  Creator) read the file directly. Schema string intentionally omitted:
  CMake errors on `$schema` below preset version 8, and version 8
  requires CMake 3.30 — outside our floor.

### Changed
- **API surface (technically breaking but unadvertised):** moved
  `delimiter_ctype.hpp` from `include/mrmr/` to `include/mrmr/detail/`.
  The class is a `std::ctype<char>` facet used internally by
  `dataset.hpp`, `continuous_dataset.hpp`, and `mixed_dataset.hpp` to
  imbue input streams during file parsing — never instantiated directly
  by user code, never documented in the README, and not referenced by
  any test. Moving to `detail/` makes the public/internal split visible
  in the layout and matches the `vcp::detail::` precedent in the sibling
  library. Public headers update their `#include` to
  `<mrmr/detail/delimiter_ctype.hpp>`. The install set (FILE_SET HEADERS
  in `CMakeLists.txt`) is updated to ship the new path so existing
  build setups that re-export the header (transitively) keep working;
  any downstream that was directly including `<mrmr/delimiter_ctype.hpp>`
  needs to update the path. The CI `lint` job's `clang-tidy` glob is
  extended to `include/mrmr/*.hpp include/mrmr/detail/*.hpp tools/*.cpp`
  so the moved header is still under static analysis.
- CI `build-and-test` job extended with a Clang matrix entry; both GCC and Clang now build
  the library, CLI, tests, and benchmarks, and run the full ctest suite at Release and Debug.
  The library is header-only and implicitly promised Clang compatibility; the matrix makes
  that promise enforceable on every PR. Matrix is `{compiler: gcc, clang} × {build_type: Release, Debug}`
  with `fail-fast: false`.
- Test and benchmark targets now compile with the same warning flags as the CLI tool
  (`-Wall -Wextra -Werror -pedantic -Wno-unused-local-typedefs`), via a new shared
  `MRMR_WARNING_FLAGS` CMake variable. Previously `test_mrmr` and the five benchmark targets
  received only `${MRMR_SANITIZE_FLAGS}`, so warnings the CLI would error on could slide
  through test or benchmark code silently. Adding a flag to `MRMR_WARNING_FLAGS` now lands
  in every consumer build at once.
- `MRMR_WARNING_FLAGS` expanded with `-Wconversion -Wsign-conversion
  -Wshadow -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough`
  plus GCC-only `-Wlogical-op` and `-Wduplicated-cond`. Mechanical fallout:
  - `attribute_information::num_values()` now `static_cast<T>(_pdf.size())`;
    T is constrained to the [0, 255] domain by the upstream
    `static_assert`, so the narrowing is value-preserving.
  - `compute_mi()` now casts the loop indices `i` and `j` to
    `value_type` at the `marginal_probability(...)` call sites.
  - `delimiter_ctype::make_table()` casts `~space` to `mask` when
    bit-clearing the table.
  - Five `bench_view_*` benchmark histogram updates
    (`++scratch[col1[i] * k + col2[i]]`) now cast the index
    expression to `std::size_t` so the implicit `unsigned char ->
    int -> size_t` chain becomes explicit.
- Catch2's INTERFACE_INCLUDE_DIRECTORIES are reassigned to
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES post-`FetchContent_MakeAvailable`,
  so warnings from Catch2's own headers (notably Clang's
  `-Wdouble-promotion` firing inside `catch_matchers_impl.hpp`'s
  float-vs-double helpers) no longer break `-Werror` builds. CMake
  3.25 added a `SYSTEM` keyword to `FetchContent_Declare` that would
  do this declaratively; the project floor is 3.24 so we move the
  property manually.
- **BREAKING**: CMake minimum requirement raised from 3.21 to 3.24. CMake 3.24 introduced `cmake -B build --fresh`, a one-command cache clobber + reconfigure that eliminates the ad-hoc `rm -rf build/CMakeCache.txt` pattern. All current target distros ship CMake >= 3.24 in their default repositories (Rocky Linux 9 AppStream = 3.26.5, Rocky Linux 10 AppStream = 3.30.5, Ubuntu 24.04 LTS = 3.28.x), so the bump imposes no new constraint on contributors. Sibling C++ libraries (`vcp`, `kdtree`) receive the same bump in coordinated PRs.
- `.gitea/workflows/ci.yml` now invokes presets instead of inline
  `-DCMAKE_BUILD_TYPE=...` / `-DMRMR_SANITIZE=ON` flags. The
  `build-and-test` matrix's `build_type: [Release, Debug]` becomes
  `preset: [release, debug]`, the `lint` job uses `cmake --preset=release`
  and `clang-tidy -p build/release` (compile_commands.json now exported
  unconditionally from the root CMakeLists), and the `sanitize` job
  uses `cmake --preset=sanitize` with `ctest --preset=sanitize`.
  Sanitizer runtime options now live on the test preset, not the
  workflow yaml.
- `CMAKE_EXPORT_COMPILE_COMMANDS` is now set unconditionally at the top
  of `CMakeLists.txt` (matching the sibling `vcp` and `kdtree`
  conventions) so editor LSPs and the `lint` CI step both find a
  compilation database without per-invocation `-D` flags.
- `.gitignore` simplified: the `build-*/` glob is removed in favor of
  the existing `build/` rule, since presets place all per-config trees
  under `build/<name>/`.
- `MRMR_SANITIZE` now enables AddressSanitizer **and** UndefinedBehaviorSanitizer (previously only ASan), applies to every built target (CLI tool, tests, benchmarks — previously only the CLI), and passes `-fno-sanitize-recover=all` so every sanitizer diagnostic is a hard error. Benchmarks drop `-O3` when the option is ON so diagnostics attribute to source lines.
- Update clang-format to v22.1.2 for fleet-wide consistency

### Fixed
- CI clang-tidy compile database generation and bugprone-branch-clone false positive on option-parsing chains
- Guard continuous-only CLI variables with MRMR_HAS_CONTINUOUS to prevent unused-variable warnings without -DMRMR_CONTINUOUS=ON
- Resolve all clang-tidy warnings across headers, CLI, and tests; align CI lint config with local .clang-tidy
- Skip the `no-commit-to-branch` pre-commit hook in CI `pre-commit` steps: the hook guards local commits to `main`/`develop` and fired spuriously when CI checked out one of those branches, failing the job despite no real commit

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
