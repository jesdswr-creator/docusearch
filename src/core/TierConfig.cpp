// ============================================================
// TierConfig.cpp
// ============================================================

#include "TierConfig.h"

namespace DocuSearch {

TierConfig TierConfigManager::getConfig(SystemTier tier) {
    TierConfig cfg;

    if (tier == SystemTier::LowEnd) {
        cfg.extractionWorkers = 1;
        cfg.ocrWorkers = 1;
        cfg.searchWorkers = 1;
        cfg.embeddingWorkers = 1;
        cfg.threadStackSize = 4 * 1024;  // KB → 4 MB after *1024

        cfg.databaseCacheSize = 8 * (1LL << 20);
        cfg.mmapSize = 0;
        cfg.journalMode = QStringLiteral("TRUNCATE");
        cfg.busyTimeout = 10000;

        cfg.maxCachedResults = 50;
        cfg.maxSearchChunkCache = 100;
        cfg.maxTextPreviewCache = 10;
        cfg.thumbnailCacheSize = 20;
        cfg.ocrOutputCacheLimit = 5;

        // Semantic search is ALWAYS on — throttled, never disabled.
        cfg.enableSemanticSearch = true;
        cfg.enableLiveIndexing = false;
        cfg.enableAutoScan = true;
        cfg.enableDuplicateDetection = true;
        cfg.enableImagePreview = false;
        cfg.enableOcrParallelism = false;
        cfg.enableBackupAutomatic = false;
        cfg.enableHybridSearch = true;

        cfg.indexingBatchSize = 25;
        cfg.indexingPauseMs = 500;
    } else if (tier == SystemTier::MidRange) {
        cfg.extractionWorkers = 3;
        cfg.ocrWorkers = 2;
        cfg.searchWorkers = 2;
        cfg.embeddingWorkers = 1;
        cfg.threadStackSize = 8 * 1024;

        cfg.databaseCacheSize = 32 * (1LL << 20);
        cfg.mmapSize = 64 * (1LL << 20);
        cfg.journalMode = QStringLiteral("WAL");
        cfg.busyTimeout = 5000;

        cfg.maxCachedResults = 200;
        cfg.maxSearchChunkCache = 500;
        cfg.maxTextPreviewCache = 50;
        cfg.thumbnailCacheSize = 100;
        cfg.ocrOutputCacheLimit = 20;

        cfg.enableSemanticSearch = true;
        cfg.enableLiveIndexing = true;
        cfg.enableAutoScan = true;
        cfg.enableDuplicateDetection = true;
        cfg.enableImagePreview = true;
        cfg.enableOcrParallelism = true;
        cfg.enableBackupAutomatic = true;
        cfg.enableHybridSearch = true;

        cfg.indexingBatchSize = 100;
        cfg.indexingPauseMs = 0;
    } else {
        cfg.extractionWorkers = 8;
        cfg.ocrWorkers = 4;
        cfg.searchWorkers = 4;
        cfg.embeddingWorkers = 1;
        cfg.threadStackSize = 16 * 1024;

        cfg.databaseCacheSize = 128 * (1LL << 20);
        cfg.mmapSize = 512 * (1LL << 20);
        cfg.journalMode = QStringLiteral("WAL");
        cfg.busyTimeout = 5000;

        cfg.maxCachedResults = 1000;
        cfg.maxSearchChunkCache = 2000;
        cfg.maxTextPreviewCache = 500;
        cfg.thumbnailCacheSize = 500;
        cfg.ocrOutputCacheLimit = 100;

        cfg.enableSemanticSearch = true;
        cfg.enableLiveIndexing = true;
        cfg.enableAutoScan = true;
        cfg.enableDuplicateDetection = true;
        cfg.enableImagePreview = true;
        cfg.enableOcrParallelism = true;
        cfg.enableBackupAutomatic = true;
        cfg.enableHybridSearch = true;

        cfg.indexingBatchSize = 200;
        cfg.indexingPauseMs = 0;
    }

    return cfg;
}

} // namespace DocuSearch
