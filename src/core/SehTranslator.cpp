// ============================================================
// SehTranslator.cpp - Convert Win32 SEH exceptions to C++ exceptions
// ============================================================

#include "SehTranslator.h"
#include "Logger.h"

#include <windows.h>
#include <eh.h>
#include <sstream>
#include <iomanip>

namespace DocuSearch {

// ── Map common SEH codes to human-readable names ────────────
const char* SehException::name(unsigned int code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT:       return "FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK:          return "FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        default:                                 return "UNKNOWN_SEH";
    }
}

// ── Build a useful "what()" string ──────────────────────────
std::string SehException::describe(unsigned int code, void* address) {
    std::ostringstream oss;
    oss << "SEH " << name(code) << " (0x" << std::hex << std::setw(8)
        << std::setfill('0') << code << ") at 0x" << std::hex
        << reinterpret_cast<uintptr_t>(address);
    return oss.str();
}

// ── The translator function ─────────────────────────────────
// Called by MSVC runtime when an SEH exception is raised inside a
// C++ try block on the same thread. We re-throw as SehException so
// the existing `catch (const std::exception&)` / `catch (...)` in
// MainWindow::onExtract() picks it up.
static void sehTranslator(unsigned int code, EXCEPTION_POINTERS* ep) {
    void* address = ep && ep->ExceptionRecord
        ? ep->ExceptionRecord->ExceptionAddress
        : nullptr;

    // Log immediately in case the re-throw itself is somehow swallowed.
    DS_ERROR("SEH", std::string("Structured exception: ") +
             SehException::name(code) +
             " (code 0x" + std::to_string(code) + ")");

    throw SehException(code, address);
}

// ── Install the translator ──────────────────────────────────
void installSehTranslator() {
    _set_se_translator(sehTranslator);
    DS_INFO("Core", "SEH-to-C++ exception translator installed");
}

} // namespace DocuSearch
