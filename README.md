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

Notes: Input data must be complete (no missing values). After discretization, attribute values are automatically compacted to contiguous integers starting from 0.

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

## Design Notes: Dataset View Access Patterns

The planned `dataset_view` class enables zero-copy bootstrap resampling for ensemble mRMR
by referencing the parent dataset through an instance index vector (with duplicates for
bootstrap). The MI hot loop must access data through this indirection. Benchmarking on this
system (x86-64, GCC 14, -O3) compared three access patterns for the joint histogram loop:

| Access Pattern | 10K instances | 100K instances | Notes |
|---|---|---|---|
| Direct sequential | 5.1 us | 59 us | Baseline: contiguous stride-1 scan |
| Sorted index indirection | 8.2 us | 90 us | +60% / +53% over baseline |
| Unsorted index indirection | 6.6 us | 108 us | +30% / +83% over baseline |
| Column materialization | — | 80 us/col | Gather cost per column |

**Key findings:**
- At N <= 10K, all data fits in L1 cache; sorting indices provides no benefit (unsorted
  is actually faster due to fewer histogram bin collision chains with clustered accesses).
- At N >= 100K, sorting indices improves spatial locality significantly (53% vs 83% overhead).
- The histogram loop is ~33% of full MI cost; the rest is probability lookups and log2.
  Sorted-index overhead on full MI is **~15-20%**, not 53%.
- Column materialization (gather + sequential MI) costs ~160 us per pair vs ~90 us for
  sorted indirection. Materialization only wins if a column is reused across many MI calls
  without eviction from cache — which is infeasible for the triangular precomputation loop
  with a small LRU cache (each column is evicted and re-materialized ~M/2 times).
- **Index sorting cost**: 4.6 ms for 100K indices (one-time). Amortized over M^2/2 MI
  calls, this is negligible.

**Design decision:** Use sorted-index indirection via `operator()` on `dataset_view`.
Sort indices at view construction time. No column materialization — the overhead is higher
than the indirection it avoids. For N <= 10K, skip the sort (not beneficial). The ~15-20%
full-MI overhead is an acceptable cost for zero-copy bootstrap capability.

## Planned Features

- Missing value support: simple imputation methods (mode, median) and pairwise complete
  observations (per-pair deletion as used in mRMRe) for mutual information computation
- mRMRe ensemble feature selection with bootstrap and exhaustive methods

## License

GPL-3.0
