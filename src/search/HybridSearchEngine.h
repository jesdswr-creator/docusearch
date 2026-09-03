#pragma once

// ============================================================
// HybridSearchEngine.h - Combines FTS5 BM25 + BGE semantic search
// ============================================================

#include <QString>
#include <vector>
#include <map>

namespace DocuSearch {

struct ExistingSearchResult {
    int    fileId     = 0;
    QString filename;
    QString path;
    QString extension;
    float   bm25Score = 0.0f;
};

struct HybridResult {
    int     fileId        = 0;
    QString filename;
    QString path;
    QString extension;
    float   keywordScore  = 0.0f;   // 0..1 (normalized BM25)
    float   semanticScore = 0.0f;   // 0..1 (cosine similarity)
    float   combinedScore = 0.0f;   // weighted average
};

class BgeService;  // forward declaration

class HybridSearchEngine {
public:
    HybridSearchEngine();

    void setBgeService(BgeService* service);
    void setSemanticEnabled(bool enabled);
    void setSemanticWeight(float weight);
    void setTopK(int k);
    void setThreshold(float t) { m_threshold = std::clamp(t, 0.0f, 1.0f); }
    float threshold() const { return m_threshold; }

    // Set the type filter (e.g., "pdf") so semantic-only results that
    // don't match the filter are excluded. Empty = no filter.
    void setTypeFilter(const QString& ext) { m_typeFilter = ext.toLower(); }

    // Combine keyword results with semantic results.
    // v1.7.5 CONTRACT — AI is strictly ADDITIVE:
    //   • Keyword results are copied verbatim in their exact BM25 order.
    //     Nothing below ever re-ranks or drops them, so turning AI ON can
    //     never make a keyword hit disappear or sink (the old RRF fusion
    //     let a weak keyword+semantic pairing outscore the #1 keyword hit,
    //     and its topK*2 cap deleted keyword rows 41..50).
    //   • Keyword hits that ALSO matched semantically keep their position
    //     and just get their semanticScore annotated (UI badge only).
    //   • Documents the keyword search missed entirely are APPENDED after
    //     the keyword list (sorted by similarity), gated by the
    //     ADDITIONS threshold (stricter than the ranking threshold —
    //     see m_additionsThreshold), the type filter, and a count budget
    //     of min(topK, max(1, round(semanticWeight * topK))).
    // Never throws — on any error, returns keyword results as-is.
    std::vector<HybridResult> search(
        const QString& queryText,
        const std::vector<ExistingSearchResult>& keywordResults);

private:
    static float normalizeScore(float bm25Score);

    BgeService* m_bgeService     = nullptr;  // not owned
    bool        m_semanticRequested = false; // what the user/UI asked for
    bool        m_semanticEnabled = false;   // request AND service present
    float       m_semanticWeight  = 0.30f;  // 30% AI, 70% keyword (read from SemanticSettings)
    // Phase 2: lowered from 0.65 (too strict for BGE-small-en-v1.5,
    // which typically returns 0.45-0.60 for genuinely related docs).
    // The old 0.65 default filtered out almost all semantic matches,
    // which is why the user said "AI has no role in search".
    float       m_threshold       = 0.45f;  // cosine similarity threshold (ranking/annotation + retrieval budget)
    // v1.7.14: semantic-ONLY additions must clear a HIGHER bar than the
    // ranking threshold. BGE-small-en-v1.5 cosine similarity for
    // UNRELATED texts commonly lands 0.40-0.55, so admitting AI-only
    // rows at the raw 0.45 threshold let borderline-noise files appear
    // as "[AI match]" on nearly every query (user report: "it will give
    // some result, but no keyword in it is related"). Truly relevant
    // query-to-passage pairs on this model score >= 0.55; 0.60 keeps the
    // AI-added rows honest — when nothing is really related, AI adds
    // nothing instead of padding the list with noise.
    float       m_additionsThreshold = 0.60f;
    int         m_topK            = 20;
    QString     m_typeFilter;               // e.g., "pdf" — filters semantic-only results
};

} // namespace DocuSearch
