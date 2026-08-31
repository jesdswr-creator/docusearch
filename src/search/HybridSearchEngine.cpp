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
    // CRITICAL FIX: re-evaluate enablement when the service arrives.
    // The old code let setSemanticEnabled(true) run during onBgeReady()
    // BEFORE this pointer was attached — setSemanticEnabled ANDed the
    // request with a null service and locked m_semanticEnabled at false
    // permanently. Symptom: every query returned keyword-only results
    // with a misleading "chunk index still building" banner, even with
    // thousands of embeddings stored and the toggle showing ON.
    m_semanticEnabled = m_semanticRequested && (m_bgeService != nullptr);
}

void HybridSearchEngine::setSemanticEnabled(bool enabled) {
    // Remember the REQUEST separately from the effective state: the
    // request survives a service pointer that arrives later.
    m_semanticRequested = enabled;
    m_semanticEnabled   = enabled && (m_bgeService != nullptr);
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
    // Short queries (1-2 words): these dominate live-search as the user
    // types, and the old 0.25 cap made the RRF deltas (~0.004) too small
    // to produce any perceivable reordering — the single biggest reason
    // toggling AI felt like a no-op. 0.35 keeps keyword relevance primary
    // while letting chunk-level semantic matches genuinely reshuffle the
    // tail of the result list.
    if (wordCount <= 2) return std::min(base, 0.35f);
    return base;
}

std::vector<HybridResult> HybridSearchEngine::search(
    const QString& queryText,
    const std::vector<ExistingSearchResult>& keywordResults) {

    try {
        const float aiWeight = computeAiWeight(queryText);
        const int K = 60;  // RRF constant

        // ── Phase 2: Adaptive RRF (Reciprocal Rank Fusion) ─────────
        // OLD (Phase 1): hardcoded `3*kwRrf + 1*semRrf` — keyword
        //   always dominated regardless of query type. Users saw no
        //   AI contribution → "AI has no role in search" complaint.
        // NEW (Phase 2): weight each side by `aiWeight` computed from
        //   query shape. Natural-language questions get up to 60%
        //   AI weight; short keyword queries get <=25% AI weight.
        //   This is the design that was intended but never wired up.

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

            // RRF: rrf_score = (1-aiWeight)/(K+keyword_rank) + aiWeight/(K+semantic_rank)
            // Phase 2: Use adaptive AI weight — natural-language queries
            // get more AI influence, short keyword queries get less.
            // This is what makes AI visibly contribute to ranking.
            int kwRank = 999, semRank = 999;
            auto krIt = keywordRank.find(fileId);
            if (krIt != keywordRank.end()) kwRank = krIt->second;
            auto srIt = semanticRank.find(fileId);
            if (srIt != semanticRank.end()) semRank = srIt->second;

            // Phase 2: Weighted RRF — each side scaled by aiWeight.
            float kwRrf  = (kwRank  < 999) ? (1.0f - aiWeight) / (K + kwRank)  : 0.0f;
            float semRrf = (semRank < 999) ? aiWeight           / (K + semRank) : 0.0f;

            // Semantic-only results (not in keyword list) must clear the
            // configured similarity bar to be included. This now follows
            // m_threshold (user-tunable via Settings, default 0.40 seeded
            // in SemanticSettings). The previous HARDCODED 0.50 gate sat
            // ABOVE the configured 0.40 threshold, so the DB happily
            // returned hits the fusion layer then threw away — a classic
            // "AI feels dead" bug: semantic-only finds were silently
            // dropped no matter what the user configured.
            if (kwRank >= 999 && semRank < 999) {
                float semSim = 0.0f;
                for (const auto& sh : semanticHits) {
                    if (sh.fileId == fileId) { semSim = sh.similarity; break; }
                }
                if (semSim < m_threshold) {
                    continue;  // skip — not similar enough to surface standalone
                }
            }

            float rrfScore = kwRrf + semRrf;
            h.combinedScore = rrfScore;

            // Store normalized scores for display.
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

        // v1.7.4 CRITICAL: the cap below used to be a blind `m_topK * 2`.
        // With the default top-K (10-20) that truncated the fused list to
        // 20-40 rows while the keyword search had returned up to 50 — any
        // keyword hit ranked below the cap was silently DROPPED whenever AI
        // mode was on ("with ai the intended result not at all showing").
        // Fusion is a RANKING layer: it must reorder the keyword results,
        // never shrink them. The cap therefore never cuts below the number
        // of keyword hits; it only bounds semantic-only ADDITIONS.
        const int floorKeep = static_cast<int>(keywordResults.size());
        const int cap = std::max(std::max(1, m_topK * 2), floorKeep);
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
