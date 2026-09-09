#pragma once

// ============================================================
// TierConfig.h - Tier-specific configuration profiles
// ============================================================

#include "SystemProfile.h"
#include <QString>

namespace DocuSearch {

struct TierConfig {
    // Thread pools
    int extractionWorkers;
    int ocrWorkers;
    int searchWorkers;
    int embeddingWorkers;
    int threadStackSize;  // KB
    
    // Database
    qint64 databaseCacheSize;  // bytes
    qint64 mmapSize;           // bytes
    QString journalMode;
    int busyTimeout;  // ms
    
    // Memory
    int maxCachedResults;
    int maxSearchChunkCache;
    int maxTextPreviewCache;  // MB
    int thumbnailCacheSize;   // MB
    int ocrOutputCacheLimit;
    
    // Features
    bool enableSemanticSearch;
    bool enableLiveIndexing;
    bool enableAutoScan;
    bool enableDuplicateDetection;
    bool enableImagePreview;
    bool enableOcrParallelism;
    bool enableBackupAutomatic;
    bool enableHybridSearch;
    
    // Indexing
    int indexingBatchSize;
    int indexingPauseMs;  // Pause between batches under load
};

class TierConfigManager {
public:
    static TierConfig getConfig(SystemTier tier);
    static TierConfig getCurrent() { return getConfig(SystemProfiler::instance()->tier()); }
};

} // namespace DocuSearch
