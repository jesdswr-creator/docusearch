// ============================================================
// BgeService.cpp - BGE service orchestrator
// ============================================================

#include "BgeService.h"
#include "../core/Logger.h"

#include <QtConcurrent>
#include <QFutureWatcher>

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
        int success = 0, fail = 0;
        for (int i = 0; i < total; ++i) {
            try {
                const int fileId = fileIds[i];
                if (!database->hasEmbedding(fileId)) {
                    std::vector<float> emb;
                    if (engine->embed(texts[i], emb) &&
                        database->storeEmbedding(fileId, emb)) {
                        ++success;
                    } else {
                        ++fail;
                    }
                } else {
                    ++success;  // already embedded counts as success
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
