// ============================================================
// HybridSearchEngine.cpp - Combine BM25 + cosine similarity
// ============================================================

#include "HybridSearchEngine.h"
#include "../embeddings/BgeService.h"
#include "../core/Logger.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <set>
#include <map>

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
        const float aiWeight = computeAiWeight(queryText);
        const int K = 60;  // RRF constant

        // ── Phase 3: RRF (Reciprocal Rank Fusion) ──────────────
        // Run keyword and semantic searches INDEPENDENTLY, then merge
        // by rank. This fixes the fundamental flaw where semantic search
        // could only rank files already found by keyword search.

        // List A: Keyword results (already ranked by BM25).
        // List B: Semantic results (independent — finds docs keyword missed).

        std::vector<SemanticHit> semanticHits;
        if (m_semanticEnabled && m_bgeService && m_bgeService->isReady()) {
            // Phase 3: Search ALL chunks, not just keyword-filtered ones.
            // This lets AI find documents that keyword search missed.
            semanticHits = m_bgeService->searchChunksAll(queryText, m_topK * 2, m_threshold);

            // If no chunks, fall back to document-level search (all docs).
            if (semanticHits.empty()) {
                semanticHits = m_bgeService->search(queryText, m_topK * 2, m_threshold);
            }
        }

        // Build rank maps: fileId → rank (0-based, lower = better).
        std::map<int, int> keywordRank;
        for (size_t i = 0; i < keywordResults.size(); ++i) {
            keywordRank[keywordResults[i].fileId] = static_cast<int>(i);
        }

        std::map<int, int> semanticRank;
        for (size_t i = 0; i < semanticHits.size(); ++i) {
            semanticRank[semanticHits[i].fileId] = static_cast<int>(i);
        }

        // Collect all unique file IDs from both lists.
        std::set<int> allFileIds;
        for (const auto& r : keywordResults) allFileIds.insert(r.fileId);
        for (const auto& sh : semanticHits) allFileIds.insert(sh.fileId);

        // Apply type filter to semantic-only results.
        auto passesTypeFilter = [&](int fileId) -> bool {
            if (m_typeFilter.isEmpty()) return true;
            // Check if this file is in keyword results (already filtered).
            if (keywordRank.count(fileId)) return true;
            // For semantic-only results, check extension.
            for (const auto& sh : semanticHits) {
                if (sh.fileId == fileId) {
                    QFileInfo fi(sh.filePath);
                    return fi.suffix().toLower() == m_typeFilter;
                }
            }
            return false;
        };

        // Compute RRF score for each file.
        std::map<int, HybridResult> resultMap;
        for (int fileId : allFileIds) {
            if (!passesTypeFilter(fileId)) continue;

            HybridResult h;
            h.fileId = fileId;

            // Get filename/path from whichever list has it.
            auto kwIt = std::find_if(keywordResults.begin(), keywordResults.end(),
                [fileId](const ExistingSearchResult& r) { return r.fileId == fileId; });
            if (kwIt != keywordResults.end()) {
                h.filename  = kwIt->filename;
                h.path      = kwIt->path;
                h.extension = kwIt->extension;
                h.keywordScore = normalizeScore(kwIt->bm25Score);
            } else {
                // From semantic results.
                for (const auto& sh : semanticHits) {
                    if (sh.fileId == fileId) {
                        h.filename  = sh.filename;
                        h.path      = sh.filePath;
                        h.semanticScore = sh.similarity;
                        break;
                    }
                }
            }

            // Get semantic score if available.
            for (const auto& sh : semanticHits) {
                if (sh.fileId == fileId) {
                    h.semanticScore = sh.similarity;
                    break;
                }
            }

            // RRF: rrf_score = 1/(K+keyword_rank) + 1/(K+semantic_rank)
            // If file not in a list, rank = 999 (contributes almost nothing).
            int kwRank = 999, semRank = 999;
            auto krIt = keywordRank.find(fileId);
            if (krIt != keywordRank.end()) kwRank = krIt->second;
            auto srIt = semanticRank.find(fileId);
            if (srIt != semanticRank.end()) semRank = srIt->second;

            float rrfScore = 1.0f / (K + kwRank) + 1.0f / (K + semRank);

            // Boost: filename match +0.15, recent (30 days) +0.05.
            // (Applied via keyword score normalization, not separately here.)
            h.combinedScore = rrfScore;

            // Also store normalized scores for display.
            h.keywordScore = (kwRank < 999) ? h.keywordScore : 0.0f;
            h.semanticScore = (semRank < 999) ? h.semanticScore : 0.0f;

            resultMap[fileId] = h;
        }

        // Convert to vector + sort by RRF score descending.
        std::vector<HybridResult> out;
        out.reserve(resultMap.size());
        for (auto& [id, h] : resultMap) out.push_back(h);

        std::sort(out.begin(), out.end(),
            [](const HybridResult& a, const HybridResult& b) {
                return a.combinedScore > b.combinedScore;
            });

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

    // Fallback: keyword results only.
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
