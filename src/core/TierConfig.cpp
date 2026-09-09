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
        cfg.threadStackSize = 4 * 1024;  // KB → 4 MB after *1024

        cfg.databaseCacheSize = 8 * (1LL << 20);
        cfg.mmapSize = 0;
        cfg.journalMode = QStringLiteral("TRUNCATE");
        cfg.busyTimeout = 10000;

        // Semantic search is ALWAYS on — throttled, never disabled.
        cfg.enableSemanticSearch = true;
        cfg.enableHybridSearch = true;
        cfg.enableLiveIndexing = false;
        cfg.enableAutoScan = true;
        cfg.enableDuplicateDetection = true;
        cfg.enableBackupAutomatic = false;

        cfg.indexingBatchSize = 25;
        cfg.indexingPauseMs = 500;
    } else if (tier == SystemTier::MidRange) {
        cfg.extractionWorkers = 3;
        cfg.ocrWorkers = 2;
        cfg.threadStackSize = 8 * 1024;

        cfg.databaseCacheSize = 32 * (1LL << 20);
        cfg.mmapSize = 64 * (1LL << 20);
        cfg.journalMode = QStringLiteral("WAL");
        cfg.busyTimeout = 5000;

        cfg.enableSemanticSearch = true;
        cfg.enableHybridSearch = true;
        cfg.enableLiveIndexing = true;
        cfg.enableAutoScan = true;
        cfg.enableDuplicateDetection = true;
        cfg.enableBackupAutomatic = true;

        cfg.indexingBatchSize = 100;
        cfg.indexingPauseMs = 0;
    } else {
        cfg.extractionWorkers = 8;
        cfg.ocrWorkers = 4;
        cfg.threadStackSize = 16 * 1024;

        cfg.databaseCacheSize = 128 * (1LL << 20);
        cfg.mmapSize = 512 * (1LL << 20);
        cfg.journalMode = QStringLiteral("WAL");
        cfg.busyTimeout = 5000;

        cfg.enableSemanticSearch = true;
        cfg.enableHybridSearch = true;
        cfg.enableLiveIndexing = true;
        cfg.enableAutoScan = true;
        cfg.enableDuplicateDetection = true;
        cfg.enableBackupAutomatic = true;

        cfg.indexingBatchSize = 200;
        cfg.indexingPauseMs = 0;
    }

    return cfg;
}

} // namespace DocuSearch
