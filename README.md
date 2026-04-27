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
- High-quality C++20 compliant code base

## Building

Requires CMake ≥ 3.24.

```bash
cmake -B build
cmake --build build
```

To force a fresh configure (drop the cached CMake state and reconfigure
from scratch — useful after changing the toolchain or build options):

```bash
cmake -B build --fresh
```

To build a sanitized Debug configuration (AddressSanitizer +
UndefinedBehaviorSanitizer on every target):

```bash
cmake -B build-san -DCMAKE_BUILD_TYPE=Debug -DMRMR_SANITIZE=ON
cmake --build build-san
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1 \
ctest --test-dir build-san --output-on-failure
```

The `sanitize` CI job runs this combination on every PR.
`-fno-sanitize-recover=all` makes every sanitizer diagnostic a hard
error; Release builds are never affected.

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
bootstrap). The MI hot loop must access data through this indirection. Extensive benchmarking
on this system (x86-64, GCC 14, -O3) compared multiple access strategies.

### Per-pair histogram benchmarks (N=100K, card=4)

| Access Pattern | Mean | vs Sequential |
|---|---|---|
| Both sequential (post-materialization) | 69 us | baseline |
| Two sorted-index indirected | 90 us | +30% |
| One materialized + one indirected | 98 us | +42% |
| Column materialization (gather cost) | 58 us/col | — |

At N=10K, all data fits in L1; sorting provides no benefit. At N=100K, sorting reduces
overhead from +83% (unsorted) to +30% (sorted) by improving spatial locality.

The mixed pattern (one materialized + one indirected) is surprisingly *slower* than two
indirected. Mismatched memory access latency between the sequential and random streams
causes pipeline stalls that offset the benefit of one sequential scan.

### End-to-end triangular cache construction (M=200, N=100K, 19900 pairs)

| Strategy | Total time | Per-pair | vs Indirection |
|---|---|---|---|
| Sorted indirection | 1.69 s | 85 us | baseline |
| Tiled B=16, materialized | 2.94 s | 148 us | +74% slower |
| Tiled B=8, materialized | 3.05 s | 153 us | +81% slower |

Cache-blocked (tiled) materialization was tested with block sizes B=8 and B=16. Each block
of B columns is gathered into contiguous buffers, then all B*(B-1)/2 within-block pairs
are computed with fully sequential access. Cross-block pairs materialize one column at a
time against the cached block. Despite reducing total materializations from M^2/2 to M^2/B,
the gather cost per column (~58 us) dominates: it exceeds the per-pair indirection savings
(90 - 69 = 21 us) unless each column is reused for ~3+ MI calls. Even B=16 (reuse factor
up to 15 for within-block) cannot compensate for the cross-block gather overhead.

### On-the-fly mRMR path (49 candidates, N=100K)

| Strategy | Total | Per-candidate |
|---|---|---|
| Two indirected | 4.15 ms | 85 us |
| Last materialized + candidate indirected | 4.24 ms | 87 us |
| Last materialized + candidate materialized | 6.75 ms | 138 us |

Materializing the last-selected column and keeping it cached provides negligible benefit
(~2% improvement) because the indirection overhead is small relative to the histogram
computation itself. Materializing each candidate column adds ~63% overhead.

### Scaling validation at N=1M

Results at N=1M (10x the primary benchmarks) confirm the same patterns:

| Access Pattern (N=1M, card=4) | Mean | vs Direct |
|---|---|---|
| Direct sequential | 668 us | baseline |
| Both materialized | 628 us | -6% |
| Sorted indirection | 932 us | +40% |
| Gather + sequential | 1289 us | +93% |

The indirection overhead stays at ~40% regardless of N (100K: +53%, 1M: +40%). The gather
cost scales linearly with N. Tiled materialization (B=8, M=50, N=1M) is 76% slower than
sorted indirection — the same ratio as at N=100K.

Cache-blocked (tiled) materialization was also tested with column reuse factors up to B=16.
Despite reducing total column materializations by B/2, the per-column gather cost (~663 us
at N=1M) exceeds the per-pair indirection savings (~264 us) by ~2.5x, so materialization
cannot amortize even at high reuse factors. The crossover would require ~3+ reuses per
gather, which the algorithm's access patterns do not provide.

Sort cost at N=1M: 54 ms (one-time, amortized over all MI calls).

### Design decision

**Sorted-index indirection wins across all tested scenarios and scales.** The indirection
overhead (~40% on the histogram loop, ~20% on full MI including log2) is the cost of
zero-copy bootstrap. Materialization cannot compensate for its gather cost at any tested N.

Implementation: `dataset_view` uses `operator()(attr, inst)` with sorted-index indirection.
Indices are sorted at view construction time. For N <= 10K, sorting is skipped (no benefit
when all data fits in L1).

### Continuous MI performance (KSG vs histogram)

When built with `-DMRMR_CONTINUOUS=ON`, KSG (k-nearest-neighbor) MI estimation is
available for continuous and mixed-type data. KSG is significantly more expensive
than histogram MI due to kd-tree construction and k-NN queries per point:

| MI Estimator | N=1K | N=5K | N=10K |
|---|---|---|---|
| Discrete (histogram, card=4) | ~1 us | ~3 us | 6 us |
| KSG (continuous, k=6) | 777 us | 4.8 ms | 10.2 ms |

KSG is ~1650x slower per MI call at N=10K. This is intrinsic to the k-NN algorithm
(O(N log N) vs O(N) for histograms). The cost is acceptable because:
- Continuous MI avoids discretization information loss
- KSG provides consistent, bias-free MI estimates without hyperparameter tuning
- For moderate attribute counts (M <= 50), full mRMR completes in seconds

Mixed-type MI dispatch (mixed_dataset, N=10K):

| Pair type | Estimator | Time per MI call |
|---|---|---|
| Discrete × discrete | Histogram | 8 us |
| Continuous × continuous | KSG | 79 ms |
| Discrete × continuous | Ross (2014) | 172 ms |

The DD path in mixed_dataset has the same performance as pure dataset<unsigned char>.
The type dispatch is one branch per MI call — zero overhead in the inner loop.

Full mRMR on continuous_dataset:

| Dataset size | Time |
|---|---|
| N=500, M=10 | 16 ms |
| N=1K, M=20 | 170 ms |

### Benchmark reproduction

Benchmarks are in `test/bench_view_access.cpp`, `test/bench_view_tiled.cpp`,
`test/bench_view_1m.cpp`, and `test/bench_continuous.cpp`. Run with:
```bash
./build/test/bench_view_access "[view-access]"
./build/test/bench_view_tiled "[tiled]"
./build/test/bench_view_1m "[1m]"
./build/test/bench_continuous "[continuous]"
```

## Planned Features

- Missing value support: simple imputation methods (mode, median) and pairwise complete
  observations (per-pair deletion as used in mRMRe) for mutual information computation
- mRMRe ensemble feature selection with bootstrap and exhaustive methods

## License

BSD-3-Clause. See [LICENSE](LICENSE).
