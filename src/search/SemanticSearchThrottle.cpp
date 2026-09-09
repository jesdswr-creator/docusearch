// ============================================================
// SemanticSearchThrottle.cpp
// ============================================================

#include "SemanticSearchThrottle.h"

namespace DocuSearch {

SemanticConfig SemanticSearchThrottle::getConfigForTier(SystemTier tier) {
    SemanticConfig cfg;
    cfg.enabled = true;  // ALWAYS enabled
    cfg.progressiveLoad = true;

    switch (tier) {
        case SystemTier::LowEnd:
            cfg.chunkBatchSize = 50;       // Small batches
            cfg.maxParallelQueries = 1;    // Serial queries
            cfg.resultCacheSize = 50;      // Minimal cache
            cfg.timeoutMs = 30000;         // 30s timeout
            break;

        case SystemTier::MidRange:
            cfg.chunkBatchSize = 100;      // Medium batches
            cfg.maxParallelQueries = 2;    // Dual queries
            cfg.resultCacheSize = 200;     // Medium cache
            cfg.timeoutMs = 15000;         // 15s timeout
            break;

        case SystemTier::HighEnd:
            cfg.chunkBatchSize = 500;      // Large batches
            cfg.maxParallelQueries = 4;    // Quad queries
            cfg.resultCacheSize = 1000;    // Large cache
            cfg.timeoutMs = 10000;         // 10s timeout
            break;
    }

    return cfg;
}

SemanticConfig SemanticSearchThrottle::getConfigUnderPressure(SystemTier tier) {
    SemanticConfig cfg = getConfigForTier(tier);

    // Under pressure: reduce parallelism, increase timeouts
    cfg.chunkBatchSize = cfg.chunkBatchSize / 2;  // Half the batch size
    cfg.maxParallelQueries = 1;                   // Force serial
    cfg.resultCacheSize = cfg.resultCacheSize / 2;  // Half the cache
    cfg.timeoutMs = cfg.timeoutMs * 2;            // Double the timeout
    cfg.progressiveLoad = true;                   // Always progressive

    return cfg;
}

SemanticConfig SemanticSearchThrottle::getAdaptiveConfig(SystemTier tier, int percentFreeRAM) {
    if (percentFreeRAM > 50) {
        return getConfigForTier(tier);  // Normal operation
    } else if (percentFreeRAM > 25) {
        // Light pressure
        auto cfg = getConfigForTier(tier);
        cfg.chunkBatchSize = cfg.chunkBatchSize * 3 / 4;
        cfg.resultCacheSize = cfg.resultCacheSize * 3 / 4;
        return cfg;
    } else {
        return getConfigUnderPressure(tier);  // Heavy pressure
    }
}

} // namespace DocuSearch
