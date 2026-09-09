#pragma once

// ============================================================
// TierConfig.h - Tier-specific configuration profiles
// ============================================================
//
// v1.7.23 (audit C2, docs/AUDIT-2026-09-09.md): every field here is
// consumed somewhere, or pinned by a test as a product invariant:
//   - extractionWorkers/threadStackSize -> main.cpp global pool
//   - ocrWorkers                        -> MainWindow OCR pool sizing
//   - databaseCacheSize/mmapSize/journalMode/busyTimeout -> Database
//   - indexingBatchSize                 -> ExtractionController
//     first-run session cap (setFirstRunSessionCap)
//   - indexingPauseMs                   -> extraction tick pacing
//     (MainWindow + GracefulDegradation healthy tick)
//   - enableLiveIndexing/enableAutoScan/enableDuplicateDetection
//                                       -> MainWindow feature gates
//   - enableBackupAutomatic             -> MainWindow daily backup
//   - enableSemanticSearch/enableHybridSearch -> pinned by tst_Tier
//     Runtime as the "AI search is throttled, never tier-disabled"
//     invariant.
// The v1.7.22 table carried 9 additional fields that nothing ever
// read (searchWorkers/embeddingWorkers, five cache-size knobs,
// enableImagePreview/enableOcrParallelism); they promised tuning the
// tier system did not do and were removed rather than wired.

#include "SystemProfile.h"
#include <QString>

namespace DocuSearch {

struct TierConfig {
    // Thread pools
    int extractionWorkers;
    int ocrWorkers;
    int threadStackSize;  // KB

    // Database
    qint64 databaseCacheSize;  // bytes
    qint64 mmapSize;           // bytes
    QString journalMode;
    int busyTimeout;  // ms

    // Features
    bool enableSemanticSearch;      // invariant: always true (throttled, never disabled)
    bool enableHybridSearch;        // invariant: always true
    bool enableLiveIndexing;        // FileWatcher live-change monitoring
    bool enableAutoScan;            // hourly + startup diff scans
    bool enableDuplicateDetection;  // Detect Duplicates action
    bool enableBackupAutomatic;     // daily automatic database backup

    // Indexing
    int indexingBatchSize;  // first-run extraction session cap
    int indexingPauseMs;    // added to the per-file tick under normal load
};

class TierConfigManager {
public:
    static TierConfig getConfig(SystemTier tier);
    static TierConfig getCurrent() { return getConfig(SystemProfiler::instance()->tier()); }
};

} // namespace DocuSearch
