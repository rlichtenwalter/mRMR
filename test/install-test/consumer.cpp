// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2018-2026 Ryan N. Lichtenwalter

// Standalone consumer compiled against an *installed* mrmr tree only.
//
// The rest of the test suite consumes the headers through the library's
// BUILD_INTERFACE, i.e. straight out of the source tree, so it stays green
// even when the FILE_SET HEADERS list omits a header and the installation is
// therefore uncompilable. This translation unit is the counterweight: it is
// built by a separate CMake project that locates mrmr via find_package(),
// with no path into the source tree at all.
//
// It includes every public header rather than only the documented entry
// points, so a header that is installed but reachable from no entry point is
// still exercised. The CI job additionally diffs the installed header set
// against include/, which catches the reverse omission.

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <mrmr/attribute_information.hpp>
#include <mrmr/continuous_dataset.hpp>
#include <mrmr/dataset.hpp>
#include <mrmr/dataset_view.hpp>
#include <mrmr/detail/delimiter_ctype.hpp>
#include <mrmr/ksg_estimator.hpp>
#include <mrmr/matrix.hpp>
#include <mrmr/mi_policy.hpp>
#include <mrmr/missing.hpp>
#include <mrmr/mixed_dataset.hpp>
#include <mrmr/mrmr.hpp>
#include <mrmr/mrmre.hpp>
#include <mrmr/typedef.hpp>

namespace {

// Smallest input that produces a non-degenerate ranking: a class column
// perfectly predicted by the first feature, plus a constant column.
constexpr char const *kSampleTsv = "class\tinformative\tconstant\n"
                                   "0\t0\t1\n"
                                   "0\t0\t1\n"
                                   "1\t1\t1\n"
                                   "1\t1\t1\n";

} // namespace

int main() {
  std::istringstream input{kSampleTsv};
  dataset<unsigned char> const data{input, dataset<unsigned char>::ROUND};

  if (data.num_instances() != 4 || data.num_attributes() != 3) {
    std::cerr << "unexpected dataset shape: " << data.num_instances() << 'x'
              << data.num_attributes() << '\n';
    return 1;
  }

  auto const [ranks, indices, names, entropies, mutual_informations, scores] = mrmr(data, 0);

  if (ranks.empty() || indices.front() != 0) {
    std::cerr << "expected the class attribute at rank 0\n";
    return 1;
  }

  std::cout << "installed-tree consumer ranked " << ranks.size() << " attributes, first is '"
            << names.front() << "'\n";
  return 0;
}
