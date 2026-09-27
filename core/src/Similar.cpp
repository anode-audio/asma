// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Similar.h"

#include "asma/core/Analysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace asma {

std::vector<SimilarMatch> findSimilar(Db& db, std::int64_t fileId, int limit)
{
    std::vector<std::int64_t> ids;
    std::vector<std::vector<double>> vectors;
    auto q = db.prepare("SELECT f.id, ft.feature_vector FROM files f JOIN roots r ON r.id = f.root_id "
                        "JOIN features ft ON ft.file_id = f.id "
                        "WHERE f.status = 'ok' AND r.enabled = 1 AND ft.feature_vector IS NOT NULL");
    std::size_t target = SIZE_MAX;
    while (q.step()) {
        const auto blob = q.getBlob(1);
        if (blob.size() != kFeatureVectorSize * sizeof(float)) continue; // written by another analyser version
        std::vector<double> v(kFeatureVectorSize);
        for (std::size_t d = 0; d < kFeatureVectorSize; ++d) {
            float x = 0.0f;
            std::memcpy(&x, blob.data() + d * sizeof(float), sizeof(float));
            v[d] = x;
        }
        if (q.getInt(0) == fileId) target = ids.size();
        ids.push_back(q.getInt(0));
        vectors.push_back(std::move(v));
    }
    if (target == SIZE_MAX || limit <= 0) return {};

    // z-score every dimension across the library.
    for (std::size_t d = 0; d < kFeatureVectorSize; ++d) {
        double mean = 0.0;
        for (const auto& v : vectors) mean += v[d];
        mean /= static_cast<double>(vectors.size());
        double var = 0.0;
        for (const auto& v : vectors) var += (v[d] - mean) * (v[d] - mean);
        const double sd = std::sqrt(var / static_cast<double>(vectors.size()));
        for (auto& v : vectors) v[d] = sd > 1e-12 ? (v[d] - mean) / sd : 0.0;
    }

    auto norm = [](const std::vector<double>& v) {
        double s = 0.0;
        for (double x : v) s += x * x;
        return std::sqrt(s);
    };
    const std::vector<double>& t = vectors[target];
    const double tn = norm(t);
    std::vector<SimilarMatch> matches;
    for (std::size_t i = 0; i < vectors.size(); ++i) {
        if (i == target) continue;
        double dot = 0.0;
        for (std::size_t d = 0; d < kFeatureVectorSize; ++d) dot += t[d] * vectors[i][d];
        const double n = tn * norm(vectors[i]);
        matches.push_back({ids[i], n > 0.0 ? dot / n : 0.0});
    }
    const auto keep = std::min<std::size_t>(matches.size(), static_cast<std::size_t>(limit));
    std::partial_sort(matches.begin(), matches.begin() + static_cast<std::ptrdiff_t>(keep), matches.end(),
                      [](const SimilarMatch& a, const SimilarMatch& b) {
                          return a.similarity != b.similarity ? a.similarity > b.similarity : a.id < b.id;
                      });
    matches.resize(keep);
    return matches;
}

} // namespace asma
