// ============================================================
// CrashHandler.cpp - Mini-dump generation on unhandled exceptions
// ============================================================

#include "CrashHandler.h"
#include "Logger.h"
#include "Config.h"

#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>

#include <QString>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QCoreApplication>

#pragma comment(lib, "dbghelp.lib")

namespace DocuSearch {

namespace {

// Path where the crash dump will be written.
QString crashDumpPath() {
    // %APPDATA%/DocuSearch/crash.dmp
    const QString dir = Config::instance().dataDir();
    QDir().mkpath(dir);
    return dir + "/crash.dmp";
}

// The exception filter. Called by Windows when an exception escapes
// all __try/__except and C++ catch handlers.
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    // 1. Log the exception code + address.
    if (ep && ep->ExceptionRecord) {
        const DWORD code = ep->ExceptionRecord->ExceptionCode;
        const void* addr  = ep->ExceptionRecord->ExceptionAddress;
        Logger::instance().error("CRASH",
            QString("Unhandled exception 0x%1 at 0x%2")
                .arg(code, 8, 16, QChar('0'))
                .arg(reinterpret_cast<quintptr>(addr), 0, 16));
    } else {
        Logger::instance().error("CRASH", "Unhandled exception (no record)");
    }

    // 2. Write the minidump.
    const QString dumpPath = crashDumpPath();
    const std::wstring widePath = dumpPath.toStdWString();

    HANDLE file = CreateFileW(widePath.c_str(),
                              GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId          = GetCurrentThreadId();
        info.ExceptionPointers = ep;
        info.ClientPointers    = FALSE;

        // MiniDumpWithDataSegs = include global + static data
        // (good for symbol resolution without being huge).
        const BOOL ok = MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            file,
            MiniDumpWithDataSegs,
            ep ? &info : nullptr,
            nullptr,  // no extra streams
            nullptr   // no callback
        );

        if (ok) {
            Logger::instance().info("CRASH",
                QString("Minidump written: %1").arg(dumpPath));
        } else {
            Logger::instance().error("CRASH",
                QString("MiniDumpWriteDump failed: error %1")
                    .arg(GetLastError()));
        }
        CloseHandle(file);
    } else {
        Logger::instance().error("CRASH",
            QString("CreateFileW failed for dump path: %1").arg(dumpPath));
    }

    // 3. Flush the logger so the log file is complete before we die.
    // (Logger is async — without this, the last few lines might be lost.)
    Logger::instance().flush();

    // 4. Show a message box so the user knows where to find the dump.
    // Use MessageBoxW directly (not QMessageBox) because Qt event loop
    // may be in a bad state at this point.
    const std::wstring msg =
        L"DocuSearch has crashed.\n\n"
        L"A crash dump has been written to:\n" + widePath +
        L"\n\n"
        L"Please send this file to the developer for diagnosis.\n"
        L"The application will now close.";
    MessageBoxW(nullptr, msg.c_str(), L"DocuSearch Crashed",
                MB_OK | MB_ICONERROR);

    // 5. Let the OS terminate the process.
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void installCrashHandler() {
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    // Don't call crashDumpPath() or DS_INFO here — both need QApplication
    // (QStandardPaths + Logger::init), which hasn't been constructed yet
    // when installCrashHandler() is called at the start of main().
    // The filter itself will compute the path when it actually fires.
}

} // namespace DocuSearch
