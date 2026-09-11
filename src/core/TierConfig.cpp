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
        // v1.7.26: WAL on LowEnd too. The old TRUNCATE journal is a
        // rollback journal — while ANY worker (extraction / embedding /
        // OCR / scan) held a write transaction, every read on the UI
        // thread BLOCKED up to busy_timeout (10 s here). That lock wait
        // was the user's "occasional freezing" on low-memory PCs: slow
        // hardware makes worker transactions longer, so they overlapped
        // UI reads constantly. WAL gives readers a snapshot — the UI
        // never waits behind a writer — at the cost of a small -shm /
        // -wal pair (~2 MB at the 500-page autocheckpoint), which is
        // irrelevant next to the freeze it removes. The network branch
        // in Database::open still forces DELETE+journal for SMB safety,
        // and the B1 read-back in Database::open pairs WAL with
        // synchronous=NORMAL (crash-safe).
        cfg.journalMode = QStringLiteral("WAL");
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
