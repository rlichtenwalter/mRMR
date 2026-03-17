# Improved mRMR

Improved mRMR is a re-implementation of the minimum redundancy maximum relevance (mRMR) feature selection algorithm with emphasis on greatly increased perfomance (1000x or greater on large data sets) and an improved user interface. There are no disadvantages to using this utility as opposed to the original release by Hanchuan Peng, but benefits include:

- Results identical to original mRMR implementation by Hanchuan Peng, excluding statistically inconsequential preservation of rank corresponce in the case of metric ties
- Incorporation of all improvements from the Fast-MRMR implementation by Sergio Ramírez
- Additional performance improvements, such as avoiding computing mutual information for zero-entropy attributes, and careful selection of and usage of data structures
- Output for each attribute includes its selection rank, entropy, mutual information with the class attribute, and mRMR score in an easily parsed format friendly to downstream manipulation
- Operates directly on original textual data, requiring no transformation into a one-time binary representation
- Robust data set parser fails gracefully with bad input and reports the location of the first error
- Modular support in the code for arbitrary discretization routines, with several examples already provided and implemented
- Support to output the result of parsing and discretization so that it can be verified and analyzed with external tools
- Supports stream-based processing, and can operate equally well reading a data set from standard input, in a pipeline, from a named pipe, or with process substitution
- Standard GNU getopt_long POSIX-compliant option processing, including full-featured `-h`/`--help` capability, informative error messages, graceful failure, and sensible defaults
- High-quality C++14 compliant code base

## Building

```bash
cmake -B build
cmake --build build
```

## Testing

```bash
ctest --test-dir build --output-on-failure
```

## Example Usage

The following two commands are equivalent in effect when run from the project top-level directory.

```bash
< example.tsv build/mrmr
build/mrmr -t '\t' -c 1 -d 'truncate' example.tsv
```

Notes: Implemented discretization functionality is minimal. Feature values are expected to be or to discretize to be contiguous integers starting from 0, but this is not currently checked.

## Installation

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

Once installed, use from CMake:

```cmake
find_package(mrmr REQUIRED)
target_link_libraries(your_target PRIVATE mrmr::mrmr)
```

Or via pkg-config:

```bash
pkg-config --cflags mrmr
```

## License

GPL-3.0
