// ============================================================
// SystemProfile.cpp
// ============================================================

#include "SystemProfile.h"

#include <QThread>
#include <QSysInfo>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <winioctl.h>
#endif

namespace DocuSearch {

SystemProfiler::SystemProfiler() {
    m_profile = detect();
}

SystemProfiler* SystemProfiler::instance() {
    static SystemProfiler inst;
    return &inst;
}

SystemProfile SystemProfiler::detect() {
    SystemProfile p;

#ifdef Q_OS_WIN
    MEMORYSTATUSEX memStatus = {};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        p.totalRAM = static_cast<qint64>(memStatus.ullTotalPhys);
        p.freeRAM  = static_cast<qint64>(memStatus.ullAvailPhys);
    }
#else
    p.totalRAM = 8LL * (1LL << 30);
    p.freeRAM  = 4LL * (1LL << 30);
#endif

    if (p.totalRAM > 0 && p.totalRAM < 6LL * (1LL << 30)) {
        p.tier = SystemTier::LowEnd;
    } else if (p.totalRAM < 32LL * (1LL << 30)) {
        p.tier = SystemTier::MidRange;
    } else {
        p.tier = SystemTier::HighEnd;
    }

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

    p.hasSSD  = detectSSD();
    p.cpuFreq = detectCPUFrequency();
    p.osVersion = QSysInfo::prettyProductName();
    p.isNetworkPath = false;

    // detect() is a pure snapshot and is called before QApplication /
    // Logger::init (main.cpp). Do not log here.

    return p;
}

void SystemProfiler::refreshFreeRAM() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memStatus = {};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        m_profile.freeRAM = static_cast<qint64>(memStatus.ullAvailPhys);
        m_profile.totalRAM = static_cast<qint64>(memStatus.ullTotalPhys);
    }
#endif
}

bool SystemProfiler::detectSSD() {
#ifdef Q_OS_WIN
    // Prefer the seek-penalty property on the system volume. FILE_READ_ATTRIBUTES
    // is enough on most consumer installs; if it fails we fall back to "assume SSD"
    // (the safer default for cache/thread sizing — over-throttling an SSD is worse
    // than slightly over-provisioning an HDD).
    HANDLE h = CreateFileW(L"\\\\.\\C:", FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        STORAGE_PROPERTY_QUERY query = {};
        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        query.QueryType  = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR desc = {};
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                                        &query, sizeof(query),
                                        &desc, sizeof(desc),
                                        &returned, nullptr);
        CloseHandle(h);
        if (ok && returned >= sizeof(desc)) {
            return desc.IncursSeekPenalty == FALSE;
        }
    }

    WCHAR root[] = L"C:\\";
    const UINT type = GetDriveTypeW(root);
    if (type == DRIVE_REMOTE || type == DRIVE_CDROM) return false;
    return true;
#else
    return true;
#endif
}

float SystemProfiler::detectCPUFrequency() {
#ifdef Q_OS_WIN
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD mhz = 0;
        DWORD size = sizeof(mhz);
        const LONG rc = RegQueryValueExW(hKey, L"~MHz", nullptr, nullptr,
                                         reinterpret_cast<LPBYTE>(&mhz), &size);
        RegCloseKey(hKey);
        if (rc == ERROR_SUCCESS && mhz > 0)
            return mhz / 1000.0f;
    }
#endif
    return 0.0f;
}

QString SystemProfiler::tierName(SystemTier t) {
    switch (t) {
        case SystemTier::LowEnd:   return QStringLiteral("LowEnd (2–4 GB)");
        case SystemTier::MidRange: return QStringLiteral("MidRange (8–16 GB)");
        case SystemTier::HighEnd:  return QStringLiteral("HighEnd (32GB+)");
    }
    return QStringLiteral("Unknown");
}

QString SystemProfiler::cpuProfileName(CPUProfile c) {
    switch (c) {
        case CPUProfile::SingleCore: return QStringLiteral("Single-Core");
        case CPUProfile::DualCore:   return QStringLiteral("Dual-Core");
        case CPUProfile::QuadCore:   return QStringLiteral("Quad-Core");
        case CPUProfile::OctoCore:   return QStringLiteral("8+ Core");
    }
    return QStringLiteral("Unknown");
}

} // namespace DocuSearch
