#pragma once

// ============================================================
// SehTranslator.h - Convert Win32 SEH exceptions to C++ exceptions
// ============================================================
//
// On Windows, Poppler, zlib, minizip and other C/C++ libraries can
// raise Structured Exception Handling (SEH) exceptions — access
// violations, stack overflows, illegal instructions — that
// `try { ... } catch (const std::exception& e) { ... }` does NOT
// catch. The default behaviour is process termination.
//
// `_set_se_translator` (MSVC-only) lets us intercept these and
// re-throw them as a C++ `SehException` that *can* be caught. This
// is what makes extraction crash-proof: a malformed PDF that
// triggers an access violation inside Poppler is caught by the
// `catch (...)` in `MainWindow::onExtract()` and logged as a failed
// file, instead of taking down the whole app.
//
// Usage:
//   1. Call `DocuSearch::installSehTranslator()` once at startup
//      (in `main.cpp`).
//   2. Any C++ `catch (const std::exception&)` or `catch (...)`
//      on the same thread will now catch SEH exceptions too.
//
// NOTE: This is MSVC-only. On MinGW/GCC the C++ exception model is
//       different and SEH is not directly translatable. DocuSearch
//       targets MSVC, so this is fine.
// ============================================================

#include <exception>
#include <string>

namespace DocuSearch {

// C++ wrapper around a Win32 SEH exception. Inherits std::exception
// so existing `catch (const std::exception&)` blocks pick it up.
class SehException : public std::exception {
public:
    SehException(unsigned int code, void* address)
        : code_(code), address_(address) {
        message_ = describe(code, address);
    }

    const char* what() const noexcept override {
        return message_.c_str();
    }

    unsigned int code() const noexcept { return code_; }
    void* address() const noexcept { return address_; }

    // Human-readable name for common SEH codes.
    static const char* name(unsigned int code);

private:
    static std::string describe(unsigned int code, void* address);

    unsigned int code_;
    void*        address_;
    std::string  message_;
};

// Install the SEH translator. Call ONCE at startup, before any
// library that might raise SEH exceptions (Poppler, zlib, Windows.Media.Ocr).
// Safe to call multiple times — re-installs the same translator.
void installSehTranslator();

} // namespace DocuSearch
