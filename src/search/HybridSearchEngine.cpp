// ============================================================
// HybridSearchEngine.cpp - Combine BM25 + cosine similarity
// ============================================================

#include "HybridSearchEngine.h"
#include "../embeddings/BgeService.h"
#include "../core/Logger.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace DocuSearch {

HybridSearchEngine::HybridSearchEngine() = default;

void HybridSearchEngine::setBgeService(BgeService* service) {
    m_bgeService = service;
}

void HybridSearchEngine::setSemanticEnabled(bool enabled) {
    m_semanticEnabled = enabled && (m_bgeService != nullptr);
}

void HybridSearchEngine::setSemanticWeight(float weight) {
    m_semanticWeight = std::clamp(weight, 0.0f, 1.0f);
}

void HybridSearchEngine::setTopK(int k) {
    m_topK = std::max(1, k);
}

float HybridSearchEngine::normalizeScore(float bm25Score) {
    // FTS5's bm25() returns NEGATIVE values (smaller = more relevant).
    const float posScore = -bm25Score;
    return 1.0f / (1.0f + std::exp(-posScore * 0.5f));
}

float HybridSearchEngine::computeAiWeight(const QString& query) const {
    // Task 3 Fix E: Query-adaptive AI/keyword weighting.
    // Natural language questions get more AI weight.
    // Short keyword-like queries get less AI weight.
    float base = m_semanticWeight;

    bool hasFieldFilter = query.contains(':');
    bool hasQuotedPhrase = query.contains('"');
    int wordCount = query.split(' ', Qt::SkipEmptyParts).size();

    // Detect natural language patterns.
    static const QStringList nlPatterns = {
        "what", "which", "find", "show", "about",
        "related", "similar", "regarding", "concerning",
        "where", "how", "who", "when", "why"
    };
    bool isNaturalLanguage = false;
    const QString lowerQuery = query.toLower();
    for (const auto& pattern : nlPatterns) {
        if (lowerQuery.startsWith(pattern) ||
            lowerQuery.contains(" " + pattern + " ")) {
            isNaturalLanguage = true;
            break;
        }
    }

    // Field filters (type:pdf) = user knows what they want → less AI.
    if (hasFieldFilter) return std::min(base, 0.15f);
    // Quoted phrases = exact match intent → less AI.
    if (hasQuotedPhrase) return std::min(base, 0.20f);
    // Natural language questions → more AI.
    if (isNaturalLanguage && wordCount > 4) return std::min(base + 0.20f, 0.60f);
    // Very short queries (1-2 words) → keyword is better.
    if (wordCount <= 2) return std::min(base, 0.25f);
    return base;
}

std::vector<HybridResult> HybridSearchEngine::search(
    const QString& queryText,
    const std::vector<ExistingSearchResult>& keywordResults) {

    try {
        // Compute adaptive AI weight based on query type.
        const float aiWeight = computeAiWeight(queryText);

        // 1. Convert keyword results to a map by fileId.
        std::map<int, HybridResult> resultMap;
        for (const auto& r : keywordResults) {
            HybridResult h;
            h.fileId        = r.fileId;
            h.filename      = r.filename;
            h.path          = r.path;
            h.extension     = r.extension;
            h.keywordScore  = normalizeScore(r.bm25Score);
            h.semanticScore = 0.0f;
            h.combinedScore = h.keywordScore * (1.0f - aiWeight);
            resultMap[r.fileId] = h;
        }

        // 2. If semantic search is enabled, merge in semantic hits.
        // Use searchFiltered — only compute cosine similarity for the files
        // already in keyword results (NOT all 100K embeddings). This is
        // the biggest AI quality + performance fix: O(N) → O(topK).
        if (m_semanticEnabled && m_bgeService && m_bgeService->isReady()) {
            // Collect file IDs from keyword results for filtered search.
            std::vector<int> keywordFileIds;
            keywordFileIds.reserve(resultMap.size());
            for (const auto& [id, h] : resultMap) {
                keywordFileIds.push_back(id);
            }

            const auto semanticHits =
                m_bgeService->searchFiltered(queryText, keywordFileIds, m_topK * 2, m_threshold);

            for (const auto& sh : semanticHits) {
                auto it = resultMap.find(sh.fileId);
                if (it != resultMap.end()) {
                    // File already in keyword results — just update its
                    // semantic score. (Type filter was already applied
                    // by the keyword search, so this is safe.)
                    it->second.semanticScore = sh.similarity;
                    it->second.combinedScore =
                        it->second.keywordScore * (1.0f - aiWeight) +
                        sh.similarity * aiWeight;
                } else {
                    // NEW semantic-only hit (not in keyword results).
                    // Apply the type filter here — if the user searched
                    // type:pdf, don't add a .txt file just because it's
                    // semantically similar.
                    if (!m_typeFilter.isEmpty()) {
                        QFileInfo fi(sh.filePath);
                        if (fi.suffix().toLower() != m_typeFilter) {
                            continue;  // skip — doesn't match type filter
                        }
                    }
                    HybridResult h;
                    h.fileId        = sh.fileId;
                    h.filename      = sh.filename;
                    h.path          = sh.filePath;
                    h.keywordScore  = 0.0f;
                    h.semanticScore = sh.similarity;
                    h.combinedScore = sh.similarity * aiWeight;
                    resultMap[sh.fileId] = h;
                }
            }
        }

        // 3. Convert to vector + sort by combined score desc.
        std::vector<HybridResult> out;
        out.reserve(resultMap.size());
        for (auto& [id, h] : resultMap) out.push_back(h);

        std::sort(out.begin(), out.end(),
            [](const HybridResult& a, const HybridResult& b) {
                return a.combinedScore > b.combinedScore;
            });

        // 4. Cap at m_topK * 2.
        const int cap = std::max(1, m_topK * 2);
        if (static_cast<int>(out.size()) > cap) {
            out.resize(cap);
        }
        return out;
    } catch (const std::exception& e) {
        DS_WARN("Hybrid", QString("Exception: %1 — falling back to keyword only.").arg(e.what()));
    } catch (...) {
        DS_WARN("Hybrid", "Unknown exception — falling back to keyword only.");
    }

    // Fallback: keyword results only, converted to HybridResult.
    std::vector<HybridResult> out;
    out.reserve(keywordResults.size());
    for (const auto& r : keywordResults) {
        HybridResult h;
        h.fileId        = r.fileId;
        h.filename      = r.filename;
        h.path          = r.path;
        h.extension     = r.extension;
        h.keywordScore  = normalizeScore(r.bm25Score);
        h.semanticScore = 0.0f;
        h.combinedScore = h.keywordScore;
        out.push_back(h);
    }
    return out;
}

} // namespace DocuSearch
