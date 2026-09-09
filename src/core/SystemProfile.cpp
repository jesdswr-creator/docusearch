// ============================================================
// SystemProfile.cpp
// ============================================================

#include "SystemProfile.h"
#include "Logger.h"
#include <QThread>
#include <QSysInfo>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <wmi.h>
#  pragma comment(lib, "wbemuuid.lib")
#endif

namespace DocuSearch {

std::unique_ptr<SystemProfiler> SystemProfiler::m_instance;

SystemProfiler::SystemProfiler() {
    m_profile = detect();
}

SystemProfiler* SystemProfiler::instance() {
    if (!m_instance) {
        m_instance = std::make_unique<SystemProfiler>();
    }
    return m_instance.get();
}

SystemProfile SystemProfiler::detect() {
    SystemProfile p;
    
    // ── RAM Detection ──
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memStatus = {};
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);
    p.totalRAM = memStatus.ullTotalPhys;
    p.freeRAM = memStatus.ullAvailPhys;
#else
    // Fallback for non-Windows
    p.totalRAM = 8 * (1LL << 30);  // 8GB default
    p.freeRAM = 4 * (1LL << 30);   // 4GB default
#endif
    
    // ── Tier Classification ──
    if (p.totalRAM < 6LL * (1LL << 30)) {  // < 6GB
        p.tier = SystemTier::LowEnd;
    } else if (p.totalRAM < 32LL * (1LL << 30)) {  // < 32GB
        p.tier = SystemTier::MidRange;
    } else {
        p.tier = SystemTier::HighEnd;
    }
    
    // ── CPU Detection ──
    p.cpuCores = QThread::idealThreadCount();
    if (p.cpuCores <= 1) {
        p.cpu = CPUProfile::SingleCore;
    } else if (p.cpuCores <= 3) {
        p.cpu = CPUProfile::DualCore;
    } else if (p.cpuCores <= 7) {
        p.cpu = CPUProfile::QuadCore;
    } else {
        p.cpu = CPUProfile::OctoCore;
    }
    
    // ── SSD Detection ──
    p.hasSSD = instance()->detectSSD();
    
    // ── CPU Frequency ──
    p.cpuFreq = instance()->detectCPUFrequency();
    
    // ── OS Version ──
    p.osVersion = QSysInfo::prettyProductName();
    
    // ── Network Path Check ──
    p.isNetworkPath = false;  // Set in Database::open() per path
    
    DS_INFO("SystemProfiler",
        QString("Detected: %1 | %2 GB RAM (free: %3 GB) | %4 cores @ %5 GHz | %6 | %7")
            .arg(tierName(p.tier))
            .arg(p.totalRAM / (1LL << 30))
            .arg(p.freeRAM / (1LL << 30))
            .arg(p.cpuCores)
            .arg(p.cpuFreq, 0, 'f', 1)
            .arg(p.hasSSD ? "SSD" : "HDD")
            .arg(p.osVersion));
    
    return p;
}

bool SystemProfiler::detectSSD() {
#ifdef Q_OS_WIN
    // Simple heuristic: check Windows registry for disk type
    // HKLM\SYSTEM\CurrentControlSet\Services\Disk\Enum -> presence of SSD indicators
    // For now, return true if C: is not a network path
    QStorageInfo storage("C:/");
    return storage.isValid() && storage.device() != "";
#else
    return false;  // Assume HDD on non-Windows
#endif
}

float SystemProfiler::detectCPUFrequency() {
#ifdef Q_OS_WIN
    // Read from registry: HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0
    // ~MHz value
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        DWORD mhz = 0;
        DWORD size = sizeof(mhz);
        if (RegQueryValueExW(hKey, L"~MHz", nullptr, nullptr,
            (LPBYTE)&mhz, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return mhz / 1000.0f;
        }
        RegCloseKey(hKey);
    }
#endif
    return 2.0f;  // Default fallback
}

QString SystemProfiler::tierName(SystemTier t) {
    switch (t) {
        case SystemTier::LowEnd: return "LowEnd (2–4 GB)";
        case SystemTier::MidRange: return "MidRange (8–16 GB)";
        case SystemTier::HighEnd: return "HighEnd (32GB+)";
    }
    return "Unknown";
}

QString SystemProfiler::cpuProfileName(CPUProfile c) {
    switch (c) {
        case CPUProfile::SingleCore: return "Single-Core";
        case CPUProfile::DualCore: return "Dual-Core";
        case CPUProfile::QuadCore: return "Quad-Core";
        case CPUProfile::OctoCore: return "8+ Core";
    }
    return "Unknown";
}

} // namespace DocuSearch
