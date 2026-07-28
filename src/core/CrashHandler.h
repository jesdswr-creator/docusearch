#pragma once

// ============================================================
// CrashHandler.h - Mini-dump generation on unhandled exceptions
// ============================================================
//
// WHY THIS EXISTS:
//   The app has been crashing during extraction for 4+ build cycles
//   with no diagnostic data. The logger only records what the app
//   explicitly logs — if a crash happens inside Poppler or ONNX, the
//   log shows nothing useful. We've been guessing at causes.
//
// WHAT THIS DOES:
//   Installs a Windows unhandled-exception filter via
//   SetUnhandledExceptionFilter(). When the app crashes:
//     1. Writes a minidump to %APPDATA%/DocuSearch/crash.dmp
//     2. Logs the exception code + address to the log file
//     3. Shows a message box telling the user where the dump is
//     4. Returns EXCEPTION_EXECUTE_HANDLER so the OS terminates cleanly
//
//   The user sends us the .dmp file. We open it in Visual Studio or
//   WinDbg with the matching PDB and get the exact crash stack trace.
//
//   One crash dump replaces 4+ build cycles of guessing.
//
// WHAT IT CATCHES:
//   - Access violations (0xC0000005)
//   - Stack overflows (0xC00000FD)
//   - Other SEH exceptions that escape _set_se_translator
//   - C++ unhandled exceptions (via _set_se_translator + terminate)
//
// WHAT IT DOESN'T CATCH:
//   - `longjmp` across C++ stack frames (Poppler can do this on
//     malformed PDFs — see comment in extraction code)
//   - Calls to abort()/terminate() that bypass the filter
//   - Heap corruption that kills the process before the filter runs
//
// DEPENDENCIES:
//   dbghelp.lib (linked in CMakeLists.txt) + dbghelp.dll (bundled)
//
// USAGE:
//   Call DocuSearch::installCrashHandler() ONCE at the start of main(),
//   before any library that might crash.
// ============================================================

namespace DocuSearch {

// Install the unhandled-exception filter. Writes minidumps to
// %APPDATA%/DocuSearch/crash.dmp (overwriting any previous dump).
// Idempotent — safe to call multiple times.
void installCrashHandler();

} // namespace DocuSearch
