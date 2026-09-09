#pragma once

// ============================================================
// SystemProfile.h - Auto-detect system tier and optimize
// ============================================================
// detect() is a pure snapshot: it never constructs the singleton.
// instance() lazily caches one snapshot for the process lifetime.
// ============================================================

#include <QString>
#include <cstdint>

namespace DocuSearch {

enum class SystemTier {
    LowEnd,      // < 6 GB RAM
    MidRange,    // 6–32 GB RAM
    HighEnd      // 32 GB+ RAM
};

enum class CPUProfile {
    SingleCore,  // 1 core
    DualCore,    // 2–3 cores
    QuadCore,    // 4–7 cores
    OctoCore,    // 8+ cores
};

struct SystemProfile {
    SystemTier tier = SystemTier::MidRange;
    CPUProfile cpu  = CPUProfile::QuadCore;
    qint64 totalRAM = 0;      // bytes
    qint64 freeRAM  = 0;      // bytes
    int cpuCores    = 1;
    bool hasSSD     = true;
    bool isNetworkPath = false;
    float cpuFreq   = 0.0f;   // GHz
    QString osVersion;
};

class SystemProfiler {
public:
    // Pure snapshot. Safe to call before QApplication / Logger::init.
    // NEVER constructs the singleton (that used to recurse on startup).
    static SystemProfile detect();

    static QString tierName(SystemTier t);
    static QString cpuProfileName(CPUProfile c);

    // Process-wide cached profile. First call runs detect() once.
    static SystemProfiler* instance();

    SystemProfile profile() const { return m_profile; }
    SystemTier tier() const { return m_profile.tier; }

    // Refresh free-RAM (cheap). Leaves the rest of the snapshot alone.
    void refreshFreeRAM();

private:
    SystemProfiler();
    SystemProfile m_profile;

    static bool detectSSD();
    static float detectCPUFrequency();
};

} // namespace DocuSearch
