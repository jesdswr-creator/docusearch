#pragma once

// ============================================================
// SemanticSearchThrottle.h - Smart semantic search throttling
// ============================================================
// Semantic search always works, but adapts to system load:
// - LowEnd: Full speed (one query at a time, no concurrency)
// - MidRange: Normal speed with result caching
// - HighEnd: Aggressive parallel queries
// - Under pressure: Reduced chunk batch size, progressive results
// ============================================================

#include "SystemProfile.h"
#include <QObject>
#include <QString>
#include <atomic>

namespace DocuSearch {

struct SemanticConfig {
    bool enabled;           // Always true (not optional)
    int chunkBatchSize;     // Chunks processed per batch
    int maxParallelQueries; // Concurrent semantic queries
    int resultCacheSize;    // Keep N results in memory
    int timeoutMs;          // Query timeout
    bool progressiveLoad;   // Show results as they arrive
};

class SemanticSearchThrottle {
public:
    static SemanticConfig getConfigForTier(SystemTier tier);
    static SemanticConfig getConfigUnderPressure(SystemTier tier);

    // Adaptive: choose config based on current memory
    static SemanticConfig getAdaptiveConfig(SystemTier tier, int percentFreeRAM);
};

} // namespace DocuSearch
