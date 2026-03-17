# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- CMake build system with install support (find_package and pkg-config)
- CTest integration with Catch2 v3 unit test suite (10 tests)
- CLI integration tests (9 tests)
- clang-format and clang-tidy configurations
- pre-commit hooks for formatting and file hygiene
- Gitea Actions CI workflow (build Release/Debug matrix, test, lint)
- Include guard for mrmr.hpp header
- CLI file-not-found error handling
- Catch2 performance benchmarks for dataset construction, mutual information, mRMR feature selection, and cardinality scaling

### Changed
- BREAKING: Headers moved to include/mrmr/ subdirectory
- BREAKING: Replaced Makefile with CMake
- CLI tool moved from src/ to tools/
- CLI version string now read from VERSION file via CMake
- Normalized include guard naming convention (MRMR_ prefix)
- All source files formatted with clang-format (LLVM style, 100 column limit)
- Mark delimiter_ctype non-template member functions as inline for ODR safety
- Move DELIMITER global variable definition out of header into each binary source

### Fixed
- Fix delimiter_ctype::make_table iterate-by-value bug that failed to clear previous space bits
- Preserve newline as whitespace in custom delimiter locale for correct header parsing

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
