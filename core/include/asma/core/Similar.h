// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>
#include <vector>

namespace asma {

struct SimilarMatch {
    std::int64_t id = 0;
    double similarity = 0.0; // cosine of z-scored feature vectors, -1..1
};

// Files that sound most like fileId, best first, from analysed feature
// vectors. Each dimension is z-scored across the library before comparing,
// so no single descriptor dominates. Only ok files in enabled roots are
// considered. Empty when fileId has no feature vector.
std::vector<SimilarMatch> findSimilar(Db& db, std::int64_t fileId, int limit = 10);

} // namespace asma
