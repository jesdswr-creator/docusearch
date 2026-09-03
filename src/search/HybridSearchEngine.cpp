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

std::vector<HybridResult> HybridSearchEngine::search(
    const QString& queryText,
    const std::vector<ExistingSearchResult>& keywordResults) {

    try {
        // ── Phase 4: Keyword-first, AI-additive fusion ──────────────
        // The old Phase 2/3 RRF (Reciprocal Rank Fusion) re-ranked the
        // whole list by (1-aiWeight)/(K+kwRank) + aiWeight/(K+semRank).
        // Two fatal flaws, both reported by users:
        //
        //   1. A file with a middling keyword rank AND any semantic rank
        //      could outscore the #1 keyword hit — turning AI ON moved
        //      (or buried) the exact document the user wanted. That is
        //      why "keyword-only is giving accurate result than
        //      AI-enabled result".
        //   2. The fused list was capped at topK*2 (default 40) —
        //      keyword results ranked 41..50 were silently DELETED in
        //      AI mode. (v1.7.4 raised the cap to the keyword count but
        //      kept the re-ranking, so flaw 1 survived.)
        //
        // New contract: AI can only ADD. Keyword results keep their
        // exact BM25 order, every one of them, always. Semantic matches
        // that keyword search missed are APPENDED after the keyword
        // list (sorted by similarity) and only if they clear the
        // similarity threshold. Keyword hits that also have a semantic
        // match just get annotated (badge "AI + keyword") — their
        // position does not move.

        // 1. Copy ALL keyword results in their original order. This is
        //    the backbone of the output — nothing below ever drops or
        //    reorders them.
        std::vector<HybridResult> out;
        out.reserve(keywordResults.size() +
                    static_cast<size_t>(std::max(1, m_topK)));
        std::set<int> keywordFileIds;
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
            keywordFileIds.insert(r.fileId);
        }

        // 2. AI off / model not ready → pure keyword (unchanged list).
        if (!m_semanticEnabled || !m_bgeService || !m_bgeService->isReady()) {
            return out;
        }

        // 3. Run the semantic scan INDEPENDENTLY over all chunks (this
        //    finds documents the keyword search missed entirely).
        const int semanticBudget = std::max(40, m_topK * 2);
        std::vector<SemanticHit> semanticHits =
            m_bgeService->searchChunksAll(queryText, semanticBudget, m_threshold);

        // No chunk hits → fall back to document-level search (all docs).
        if (semanticHits.empty()) {
            semanticHits = m_bgeService->search(queryText, semanticBudget, m_threshold);
        }

        // Map fileId → semantic similarity for annotation + additions.
        std::map<int, float> simByFile;
        for (const auto& sh : semanticHits) {
            simByFile[sh.fileId] = sh.similarity;
        }

        // 4. Annotate keyword hits that also have a semantic match.
        //    Position untouched — the score only feeds the UI badge.
        for (auto& h : out) {
            auto it = simByFile.find(h.fileId);
            if (it != simByFile.end()) {
                h.semanticScore = it->second;
            }
        }

        // 5. Append semantic-only finds (files keyword search missed).
        //    The number of additions is scaled by the user's AI weight
        //    and capped by topK ("Maximum AI Results").
        if (m_semanticWeight > 0.0f && !semanticHits.empty()) {
            const int additionsAllowed = std::min(
                m_topK,
                std::max(1, static_cast<int>(std::lround(
                    m_semanticWeight * static_cast<float>(m_topK)))));

            int added = 0;
            for (const auto& sh : semanticHits) {
                if (added >= additionsAllowed) break;
                if (keywordFileIds.count(sh.fileId)) continue;  // already listed
                // v1.7.14: additions clear the STRICTER additions bar, not
                // the raw ranking threshold — the #1 complaint "AI results
                // contain no keyword I typed" came from unrelated files
                // scoring 0.45-0.55 (the model's noise floor) and being
                // appended anyway. Below 0.60 the AI now adds nothing.
                if (sh.similarity < m_additionsThreshold) continue;

                // Respect the type filter (e.g. type:pdf must not show .txt).
                if (!m_typeFilter.isEmpty()) {
                    if (sh.filePath.isEmpty()) continue;  // no way to verify
                    if (QFileInfo(sh.filePath).suffix().toLower() != m_typeFilter)
                        continue;
                }

                HybridResult h;
                h.fileId        = sh.fileId;
                h.filename      = sh.filename;
                h.path          = sh.filePath;
                h.extension     = sh.filePath.isEmpty()
                    ? QString()
                    : QFileInfo(sh.filePath).suffix().toLower();
                h.keywordScore  = 0.0f;
                h.semanticScore = sh.similarity;
                h.combinedScore = sh.similarity;
                out.push_back(h);
                ++added;
            }
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
