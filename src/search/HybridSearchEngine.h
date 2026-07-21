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

    // Combine keyword results with semantic results.
    // Never throws — on any error, returns keyword results as-is.
    std::vector<HybridResult> search(
        const QString& queryText,
        const std::vector<ExistingSearchResult>& keywordResults);

private:
    static float normalizeScore(float bm25Score);

    BgeService* m_bgeService     = nullptr;  // not owned
    bool        m_semanticEnabled = false;
    float       m_semanticWeight  = 0.40f;
    int         m_topK            = 20;
};

} // namespace DocuSearch
