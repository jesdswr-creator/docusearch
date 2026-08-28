// ============================================================
// BgeService.cpp - BGE service orchestrator
// ============================================================

#include "BgeService.h"
#include "../core/Logger.h"
#include "../core/SehTranslator.h"

#include <QtConcurrent>
#include <QFutureWatcher>
#include <algorithm>
#include <map>
#include <vector>

namespace DocuSearch {

BgeService::BgeService(QObject* parent)
    : QObject(parent) {
}

BgeService::~BgeService() = default;

bool BgeService::initialize(const QString& dbPath, const QString& modelPath) {
    try {
        // 1. Create + initialize engine
        m_engine = std::make_unique<BgeEmbeddingEngine>();
        if (!m_engine->initialize(modelPath)) {
            m_statusMessage = "BGE model not found or failed to load: " + modelPath;
            DS_WARN("BGE", m_statusMessage);
            m_initialized = false;
            return false;
        }

        // 2. Create + open database
        m_database = std::make_unique<BgeEmbeddingDb>(dbPath);
        if (!m_database->open()) {
            m_statusMessage = "Failed to open embedding database at " + dbPath;
            DS_WARN("BGE", m_statusMessage);
            m_initialized = false;
            return false;
        }

        m_initialized   = true;
        const auto stats = m_database->getStats();
        m_statusMessage = QString("Ready (%1 embeddings)").arg(stats.total);
        DS_INFO("BGE", "BGE service initialized. " + m_statusMessage);
        emit ready();
        return true;
    } catch (const std::bad_alloc& e) {
        m_statusMessage = QString("OOM during BGE init: %1").arg(e.what());
        DS_WARN("BGE", m_statusMessage);
    } catch (const std::exception& e) {
        m_statusMessage = QString("Exception during BGE init: %1").arg(e.what());
        DS_WARN("BGE", m_statusMessage);
    } catch (...) {
        m_statusMessage = "Unknown exception during BGE init.";
        DS_WARN("BGE", m_statusMessage);
    }
    m_initialized = false;
    return false;
}

std::vector<SemanticHit> BgeService::search(
    const QString& query, int topK, float threshold) {
    if (!m_initialized) return {};
    try {
        std::vector<float> queryEmbed;
        // BGE instruction prefix: prepend instruction for query embedding
        // (NOT for document embedding — BGE uses asymmetric design).
        // See Task 3 Fix A in review report.
        const QString prefixedQuery = BgeEmbeddingEngine::queryPrefix() + query;
        if (!m_engine->embed(prefixedQuery, queryEmbed)) return {};
        return m_database->searchSimilar(queryEmbed, topK, threshold);
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Exception during search: %1").arg(e.what()));
    } catch (...) {
        DS_WARN("BGE", "Unknown exception during search.");
    }
    return {};
}

std::vector<SemanticHit> BgeService::searchFiltered(
    const QString& query, const std::vector<int>& fileIds,
    int topK, float threshold) {
    if (!m_initialized) return {};
    try {
        std::vector<float> queryEmbed;
        const QString prefixedQuery = BgeEmbeddingEngine::queryPrefix() + query;
        if (!m_engine->embed(prefixedQuery, queryEmbed)) return {};
        return m_database->searchSimilarFiltered(queryEmbed, fileIds, topK, threshold);
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Exception during filtered search: %1").arg(e.what()));
    } catch (...) {
        DS_WARN("BGE", "Unknown exception during filtered search.");
    }
    return {};
}

std::vector<SemanticHit> BgeService::searchChunksFiltered(
    const QString& query, const std::vector<int>& fileIds,
    int topK, float threshold) {
    if (!m_initialized) return {};
    try {
        std::vector<float> queryEmbed;
        const QString prefixedQuery = BgeEmbeddingEngine::queryPrefix() + query;
        if (!m_engine->embed(prefixedQuery, queryEmbed)) return {};
        return m_database->searchSimilarChunks(queryEmbed, fileIds, topK, threshold);
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Exception during chunk search: %1").arg(e.what()));
    } catch (...) {
        DS_WARN("BGE", "Unknown exception during chunk search.");
    }
    return {};
}

std::vector<SemanticHit> BgeService::searchChunksAll(
    const QString& query, int topK, float threshold) {
    if (!m_initialized) return {};
    try {
        std::vector<float> queryEmbed;
        const QString prefixedQuery = BgeEmbeddingEngine::queryPrefix() + query;
        if (!m_engine->embed(prefixedQuery, queryEmbed)) return {};

        // Precision path: best chunk per file.
        auto chunkHits = m_database->searchSimilarChunksAll(queryEmbed, topK, threshold);
        const float bestChunk = m_database->lastBestSimilarity();

        // Full-document path. CRITICAL for indexes built before chunked
        // embedding existed: those files have NO EmbeddingChunks rows, so
        // a chunks-only scan made the whole semantic index look dead
        // ("no embeddings to compare yet" forever, despite thousands of
        // embedded documents). Scanning document embeddings as well keeps
        // them searchable while the chunk backfill drains.
        auto docHits = m_database->searchSimilar(queryEmbed, topK, threshold);
        const float bestDoc = m_database->lastBestSimilarity();

        if (chunkHits.empty()) return docHits;    // document-level only
        if (docHits.empty())   return chunkHits;  // chunks only

        // Merge per file — the better of (best chunk, full document).
        // Document-level hits carry path/filename metadata; chunk hits do
        // not, so keep whichever entry is richer when upgrading similarity.
        std::map<int, SemanticHit> merged;
        for (auto& h : docHits)   merged[h.fileId] = std::move(h);
        for (auto& h : chunkHits) {
            auto it = merged.find(h.fileId);
            if (it == merged.end()) {
                merged[h.fileId] = std::move(h);
            } else if (h.similarity > it->second.similarity) {
                h.filePath = it->second.filePath;
                h.filename = it->second.filename;
                merged[h.fileId] = std::move(h);
            }
        }
        std::vector<SemanticHit> out;
        out.reserve(merged.size());
        for (auto& kv : merged) {
            Q_UNUSED(kv.first);
            out.push_back(std::move(kv.second));
        }
        std::sort(out.begin(), out.end(),
            [](const SemanticHit& a, const SemanticHit& b) {
                return a.similarity > b.similarity;
            });
        if (static_cast<int>(out.size()) > topK) out.resize(topK);

        // The two scans each overwrote the diagnostic; report the true best.
        m_database->setLastBestSimilarity(std::max(bestChunk, bestDoc));
        return out;
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Exception during chunk search all: %1").arg(e.what()));
    } catch (...) {
        DS_WARN("BGE", "Unknown exception during chunk search all.");
    }
    return {};
}

bool BgeService::embedDocument(int fileId, const QString& text) {
    if (!m_initialized) return false;
    try {
        if (m_database->hasEmbedding(fileId)) return true;
        std::vector<float> embedding;
        if (!m_engine->embed(text, embedding)) return false;
        return m_database->storeEmbedding(fileId, embedding);
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Exception embedding file %1: %2").arg(fileId).arg(e.what()));
    } catch (...) {
        DS_WARN("BGE", QString("Unknown exception embedding file %1").arg(fileId));
    }
    return false;
}

bool BgeService::embedDocumentChunked(int fileId, const QString& text) {
    if (!m_initialized) return false;
    try {
        // If already has chunks, skip.
        if (m_database->hasChunks(fileId)) return true;

        // Also store a single full-document embedding (for backward compat).
        if (!m_database->hasEmbedding(fileId)) {
            std::vector<float> fullEmbed;
            if (m_engine->embed(text, fullEmbed)) {
                m_database->storeEmbedding(fileId, fullEmbed);
            }
        }

        // Split text into chunks of ~1000 chars (approx 256 tokens) with
        // ~250 char overlap (approx 64 tokens). Simple character-based
        // chunking — not token-accurate but close enough for quality.
        const int CHUNK_SIZE = 1000;
        const int OVERLAP = 250;
        const int textLen = text.length();

        if (textLen <= CHUNK_SIZE) {
            // Short document — single chunk, same as existing embedding.
            return true;
        }

        std::vector<BgeEmbeddingDb::ChunkData> chunks;
        int chunkIndex = 0;
        int offset = 0;

        while (offset < textLen) {
            int end = std::min(offset + CHUNK_SIZE, textLen);

            // Try to split at sentence boundary (period + space).
            if (end < textLen) {
                int sentenceEnd = text.lastIndexOf(". ", end);
                if (sentenceEnd > offset + CHUNK_SIZE / 2) {
                    end = sentenceEnd + 1;
                }
            }

            QString chunkText = text.mid(offset, end - offset);
            std::vector<float> chunkEmbed;
            if (m_engine->embed(chunkText, chunkEmbed)) {
                BgeEmbeddingDb::ChunkData cd;
                cd.chunkIndex = chunkIndex;
                cd.startOffset = offset;
                cd.endOffset = end;
                cd.embedding = std::move(chunkEmbed);
                chunks.push_back(std::move(cd));
            }

            chunkIndex++;
            offset = end - OVERLAP;
            if (offset >= textLen) break;
            // Cap at 50 chunks to prevent runaway on huge files.
            if (chunkIndex >= 50) break;
        }

        if (!chunks.empty()) {
            m_database->storeChunks(fileId, chunks);
            DS_INFO("BGE", QString("Stored %1 chunks for file %2").arg(chunks.size()).arg(fileId));
        }
        return true;
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Exception chunking file %1: %2").arg(fileId).arg(e.what()));
    } catch (...) {
        DS_WARN("BGE", QString("Unknown exception chunking file %1").arg(fileId));
    }
    return false;
}

void BgeService::embedDocumentsBatch(const QVector<int>& fileIds,
                                      const QStringList& texts) {
    if (!m_initialized) {
        emit embeddingFinished(0, 0);
        return;
    }

    // Capture raw pointers + flag — service lifetime is managed by
    // MainWindow and outlives the background operation.
    auto* engine   = m_engine.get();
    auto* database = m_database.get();
    const int total = std::min(fileIds.size(), texts.size());

    // Run on Qt's global thread pool.
    auto* watcher = new QFutureWatcher<void>(this);
    auto future = QtConcurrent::run([this, watcher, engine, database,
                                     fileIds, texts, total]() {
        // CRITICAL: install the SEH translator ON THIS THREAD.
        // _set_se_translator() is per-thread; without this, an access
        // violation inside ONNX Runtime would crash the process instead
        // of being caught by the catch(...) below.
        installSehTranslator();

        int success = 0, fail = 0;
        for (int i = 0; i < total; ++i) {
            try {
                const int fileId = fileIds[i];
                // Phase 2: generate CHUNKED embeddings (one per ~1000-char
                //   chunk) in addition to the single full-document embedding.
                //   This populates the EmbeddingChunks table that
                //   HybridSearchEngine::search() calls via searchChunksAll().
                //   Previously, only `storeEmbedding` was called, so the
                //   chunks table stayed empty, and searchChunksAll() always
                //   fell back to the slower single-embedding search path.
                //   This is a small per-document cost (one inference per
                //   chunk) but delivers ~5x more precise semantic results
                //   for long documents because we match the best chunk.
                bool ok = false;
                if (!database->hasEmbedding(fileId)) {
                    std::vector<float> emb;
                    if (engine->embed(texts[i], emb) &&
                        database->storeEmbedding(fileId, emb)) {
                        ++success;
                        ok = true;
                    } else {
                        ++fail;
                    }
                } else {
                    ++success;  // already embedded counts as success
                    ok = true;
                }

                // Generate chunked embeddings if not already present.
                // embedDocumentChunked() is safe to call from this worker
                // thread — the ONNX session is internally synchronized.
                if (ok && !database->hasChunks(fileId)) {
                    const QString& docText = texts[i];
                    // Reuse the engine to generate per-chunk embeddings.
                    // Mirror embedDocumentChunked() but inlined so we
                    // don't need an extra public method on BgeService.
                    const int CHUNK_SIZE = 1000;
                    const int OVERLAP = 250;
                    const int textLen = docText.length();
                    if (textLen > CHUNK_SIZE) {
                        std::vector<BgeEmbeddingDb::ChunkData> chunks;
                        int chunkIndex = 0;
                        int offset = 0;
                        while (offset < textLen && chunkIndex < 50) {
                            int end = std::min(offset + CHUNK_SIZE, textLen);
                            // Try to split at sentence boundary.
                            if (end < textLen) {
                                int sentenceEnd = docText.lastIndexOf(". ", end);
                                if (sentenceEnd > offset + CHUNK_SIZE / 2) {
                                    end = sentenceEnd + 1;
                                }
                            }
                            QString chunkText = docText.mid(offset, end - offset);
                            std::vector<float> chunkEmb;
                            if (engine->embed(chunkText, chunkEmb)) {
                                BgeEmbeddingDb::ChunkData cd;
                                cd.chunkIndex = chunkIndex;
                                cd.startOffset = offset;
                                cd.endOffset = end;
                                cd.embedding = std::move(chunkEmb);
                                chunks.push_back(std::move(cd));
                            }
                            ++chunkIndex;
                            offset = end - OVERLAP;
                            if (offset >= textLen) break;
                        }
                        if (!chunks.empty()) {
                            database->storeChunks(fileId, chunks);
                        }
                    }
                }
            } catch (...) {
                ++fail;
            }
            // Emit progress on the main thread.
            QMetaObject::invokeMethod(this, [this, i, total]() {
                emit embeddingProgress(i + 1, total);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, success, fail]() {
            emit embeddingFinished(success, fail);
        }, Qt::QueuedConnection);
    });
    watcher->setFuture(future);
    connect(watcher, &QFutureWatcher<void>::finished, watcher, &QObject::deleteLater);
}

BgeEmbeddingDb::Stats BgeService::getStats() const {
    if (!m_database) return {};
    return m_database->getStats();
}

} // namespace DocuSearch
