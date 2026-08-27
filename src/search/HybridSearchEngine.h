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
    // Never throws — on any error, returns keyword results as-is.
    std::vector<HybridResult> search(
        const QString& queryText,
        const std::vector<ExistingSearchResult>& keywordResults);

private:
    static float normalizeScore(float bm25Score);
    float computeAiWeight(const QString& query) const;

    BgeService* m_bgeService     = nullptr;  // not owned
    bool        m_semanticEnabled = false;
    float       m_semanticWeight  = 0.30f;  // 30% AI, 70% keyword (read from SemanticSettings)
    // Phase 2: lowered from 0.65 (too strict for BGE-small-en-v1.5,
    // which typically returns 0.45-0.60 for genuinely related docs).
    // The old 0.65 default filtered out almost all semantic matches,
    // which is why the user said "AI has no role in search".
    float       m_threshold       = 0.45f;  // cosine similarity threshold
    int         m_topK            = 20;
    QString     m_typeFilter;               // e.g., "pdf" — filters semantic-only results
};

} // namespace DocuSearch
