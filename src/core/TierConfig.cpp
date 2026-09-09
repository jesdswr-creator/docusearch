// ============================================================
// TierConfig.cpp
// ============================================================

#include "TierConfig.h"

namespace DocuSearch {

TierConfig TierConfigManager::getConfig(SystemTier tier) {
    TierConfig cfg;
    
    if (tier == SystemTier::LowEnd) {
        // ── LOW-END: 2–4GB RAM, Dual-core ──
        cfg.extractionWorkers = 1;
        cfg.ocrWorkers = 1;
        cfg.searchWorkers = 1;
        cfg.embeddingWorkers = 1;
        cfg.threadStackSize = 4 * 1024;  // 4MB
        
        cfg.databaseCacheSize = 8 * (1LL << 20);   // 8MB
        cfg.mmapSize = 0;                          // Disable mmap
        cfg.journalMode = "TRUNCATE";
        cfg.busyTimeout = 10000;
        
        cfg.maxCachedResults = 50;
        cfg.maxSearchChunkCache = 100;
        cfg.maxTextPreviewCache = 10;
        cfg.thumbnailCacheSize = 20;
        cfg.ocrOutputCacheLimit = 5;
        
        cfg.enableSemanticSearch = false;     // User can override
        cfg.enableLiveIndexing = false;       // Manual scan only
        cfg.enableAutoScan = false;
        cfg.enableDuplicateDetection = true;
        cfg.enableImagePreview = false;
        cfg.enableOcrParallelism = false;
        cfg.enableBackupAutomatic = false;
        cfg.enableHybridSearch = false;
        
        cfg.indexingBatchSize = 25;
        cfg.indexingPauseMs = 500;
        
    } else if (tier == SystemTier::MidRange) {
        // ── MID-RANGE: 8–16GB RAM, Quad-core ──
        cfg.extractionWorkers = 3;
        cfg.ocrWorkers = 2;
        cfg.searchWorkers = 2;
        cfg.embeddingWorkers = 1;
        cfg.threadStackSize = 8 * 1024;  // 8MB
        
        cfg.databaseCacheSize = 32 * (1LL << 20);  // 32MB
        cfg.mmapSize = 64 * (1LL << 20);           // 64MB
        cfg.journalMode = "WAL";
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
        
    } else {  // HighEnd
        // ── HIGH-END: 32GB+ RAM, 8+ cores ──
        cfg.extractionWorkers = 8;
        cfg.ocrWorkers = 4;
        cfg.searchWorkers = 4;
        cfg.embeddingWorkers = 1;
        cfg.threadStackSize = 16 * 1024;  // 16MB
        
        cfg.databaseCacheSize = 128 * (1LL << 20);  // 128MB
        cfg.mmapSize = 512 * (1LL << 20);           // 512MB
        cfg.journalMode = "WAL";
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
