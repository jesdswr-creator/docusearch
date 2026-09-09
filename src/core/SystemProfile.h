#pragma once

// ============================================================
// SystemProfile.h - Auto-detect system tier and optimize accordingly
// ============================================================
// Detects: RAM, CPU cores, disk type (SSD/HDD), network storage
// Assigns: Tier (LowEnd/MidRange/HighEnd)
// Applied: Thread pools, cache sizes, feature flags
// ============================================================

#include <QString>
#include <cstdint>
#include <memory>

namespace DocuSearch {

enum class SystemTier {
    LowEnd,      // 2–4GB RAM
    MidRange,    // 8–16GB RAM
    HighEnd      // 32GB+ RAM
};

enum class CPUProfile {
    SingleCore,  // 1 core
    DualCore,    // 2–3 cores
    QuadCore,    // 4–7 cores
    OctoCore,    // 8+ cores
};

struct SystemProfile {
    SystemTier tier;
    CPUProfile cpu;
    qint64 totalRAM;      // bytes
    qint64 freeRAM;       // bytes
    int cpuCores;
    bool hasSSD;
    bool isNetworkPath;
    float cpuFreq;        // GHz
    QString osVersion;
};

class SystemProfiler {
public:
    // Detect system on startup
    static SystemProfile detect();
    
    // Convert to string for logging
    static QString tierName(SystemTier t);
    static QString cpuProfileName(CPUProfile c);
    
    // Singleton access
    static SystemProfiler* instance();
    
    SystemProfile profile() const { return m_profile; }
    SystemTier tier() const { return m_profile.tier; }
    
private:
    SystemProfiler();
    static std::unique_ptr<SystemProfiler> m_instance;
    SystemProfile m_profile;
    
    bool detectSSD();
    float detectCPUFrequency();
};

} // namespace DocuSearch
