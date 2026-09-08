#pragma once

// ============================================================
// NativeSplash.h — thread-driven splash screen (v1.7.20)
// ============================================================
//
// WHY THIS EXISTS. The splash animation was a Qt widget for four
// releases, and users kept reporting "still stuck or glitch". The
// honest engineering verdict (documented in SplashOverlay.h): a Qt
// widget can only repaint on the UI thread, and the UI thread is busy
// for long stretches while MainWindow constructs (widget tree, QSS,
// icon decode). Pumps and shorter constructors only shrink the stalls —
// nothing can remove them while the animation lives on the UI thread.
//
// So the animation MOVED OFF the UI thread:
//
//   * On Windows (this app's only shipping platform) NativeSplash owns
//     a real Win32 layered window created on its own std::thread and
//     presents frames via UpdateLayeredWindow() every 16 ms. The UI
//     thread can stall for SECONDS — the splash still runs at full
//     frame rate, because DWM composites the layered window
//     independently of anything the main thread does.
//   * The artwork is the SHARED renderer (SplashOverlay.h:
//     renderSplashFrame) painted into a premultiplied ARGB QImage on
//     the splash thread — one source of truth, pixel-identical to the
//     widget fallback.
//   * The fade-out animates SourceConstantAlpha in the same thread —
//     again independent of the (possibly busy) UI thread. The optional
//     callback runs afterwards on the UI thread via
//     QMetaObject::invokeMethod, preserving main.cpp's reveal order
//     (splash fades over the desktop, THEN the main window shows).
//   * If anything in the Win32 path fails (exotic session, class
//     registration, window creation), show() transparently falls back
//     to the old Qt widget splash — a guaranteed-visible splash either
//     way.
// ============================================================

#include "SplashOverlay.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QImage>
#include <QPainter>
#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace DocuSearch {

class NativeSplash {
public:
    NativeSplash() = default;
    ~NativeSplash() { stopAndJoin(); }

    NativeSplash(const NativeSplash&) = delete;
    NativeSplash& operator=(const NativeSplash&) = delete;

    // Theme colors — the namespace-scope SplashThemeColors (aliased as
    // SplashOverlay::ThemeColors for the widget fallback).
    void setThemeColors(const SplashThemeColors& c) { colors_ = c; }

    bool isVisible() const {
        if (mode_ == Mode::Widget && widget_)
            return widget_->isVisible();
        return shown_.load();
    }

    void show() {
#ifdef Q_OS_WIN
        if (showNative()) {
            mode_ = Mode::Native;
            return;
        }
#endif
        // Fallback: the Qt widget path (non-Windows builds, or any
        // native failure). Same artwork, same API.
        widget_ = std::make_unique<SplashOverlay>();
        widget_->setThemeColors(colors_);
        widget_->show();
        mode_ = Mode::Widget;
    }

    // Fade out over ~200 ms and close. The optional callback runs on the
    // UI thread AFTER the splash is gone (main.cpp uses it to show the
    // main window — reveal order: splash fades over the desktop first).
    // Safe to call more than once; a hidden splash closes immediately.
    void fadeOutAndClose(std::function<void()> after = {}) {
        if (mode_ == Mode::Widget) {
            if (widget_) widget_->fadeOutAndClose(std::move(after));
            return;
        }
        if (mode_ != Mode::Native) {
            if (after) after();
            return;
        }
        {
            std::lock_guard<std::mutex> lk(cbMutex_);
            after_ = std::move(after);
        }
        if (closeRequested_.exchange(true)) return;   // already fading
        // The thread notices the flag within one 16 ms tick, fades for
        // 200 ms, destroys the window, posts the callback to the UI
        // thread and exits. The dtor joins it if the process is exiting.
    }

private:
    enum class Mode { None, Native, Widget };

#ifdef Q_OS_WIN
    // ---- Win32 plumbing -------------------------------------------------

    static LRESULT CALLBACK splashWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_PAINT:          // ULW presents the pixels; nothing to paint
            ValidateRect(h, nullptr);
            return 0;
        case WM_NCHITTEST:      // never interactive
            return HTCLIENT;
        default:
            return DefWindowProcW(h, msg, w, l);
        }
    }

    bool showNative() {
        // All Win32 setup happens ON THE SPLASH THREAD (window ownership
        // lives there). show() waits up to 250 ms for "shown or failed"
        // — on the healthy path the thread signals within a few ms; the
        // wait only runs to full length on the failure path, where we
        // then fall back to the widget.
        try {
            thread_ = std::thread(&NativeSplash::threadProc, this);
        } catch (...) {
            return false;   // thread spawn failed — widget fallback
        }
        std::unique_lock<std::mutex> lk(stateMutex_);
        if (!stateCv_.wait_for(lk, std::chrono::milliseconds(250), [this] {
                return shown_.load() || failed_.load();
            })) {
            // Did not report in time — treat as failed and fall back; the
            // thread (if it wakes late) sees closeRequested_ and exits
            // without ever having shown anything persistent.
            failed_.store(true);
            closeRequested_.store(true);
            return false;
        }
        return shown_.load() && !failed_.load();
    }

    void threadProc() {
        static constexpr wchar_t kClassName[] = L"DocuSearchSplashWnd";

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &NativeSplash::splashWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor       = LoadCursorW(nullptr, IDC_APPSTARTING);
        if (!RegisterClassExW(&wc)) {
            // Idempotent: a previous run of this process registered it.
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                failAndExit();
                return;
            }
        }

        // Physical size from the DPI of the monitor the cursor is on —
        // pure Win32 units, no Qt coordinate conversion involved.
        POINT cur{};
        GetCursorPos(&cur);
        HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kClassName, L"DocuSearch", WS_POPUP,
            0, 0, kSplashW, kSplashH,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd) {
            failAndExit();
            return;
        }
        // v1.7.20: GetDpiForWindow dynamically — a static import would make
        // the whole exe fail to LOAD on Windows versions older than 1607.
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        const GetDpiForWindowFn pGetDpi = []() -> GetDpiForWindowFn {
            HMODULE u32 = GetModuleHandleW(L"user32.dll");
            return u32 ? reinterpret_cast<GetDpiForWindowFn>(
                             GetProcAddress(u32, "GetDpiForWindow"))
                       : nullptr;
        }();
        UINT dpi = 96;
        if (pGetDpi) dpi = pGetDpi(hwnd);
        if (dpi < 96 || dpi > 960) dpi = 96;   // sane bounds
        const int wPhys = MulDiv(kSplashW, int(dpi), 96);
        const int hPhys = MulDiv(kSplashH, int(dpi), 96);
        int x = 0, y = 0;
        if (mon && GetMonitorInfoW(mon, &mi)) {
            x = mi.rcWork.left + ((mi.rcWork.right  - mi.rcWork.left) - wPhys) / 2;
            y = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top ) - hPhys) / 2;
        } else {
            x = (GetSystemMetrics(SM_CXSCREEN) - wPhys) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - hPhys) / 2;
        }
        // Resources for presentation (created before the race check so
        // the early-exit path can clean them up).
        HDC screenDc = GetDC(nullptr);
        HDC memDc    = CreateCompatibleDC(screenDc);
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, wPhys, hPhys, SWP_NOACTIVATE);
        if (closeRequested_.load()) {   // shutdown raced the setup
            DestroyWindow(hwnd);
            ReleaseDC(nullptr, screenDc);
            DeleteDC(memDc);
            failAndExit();
            return;
        }
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        QElapsedTimer clock;
        clock.start();
        shown_.store(true);
        stateCv_.notify_all();

        int  fadeAlpha = 255;
        qint64 fadeStartMs = -1;
        bool destroyed = false;
        while (!destroyed) {
            // Pump the (rare) messages: WM_CLOSE from a fast shutdown.
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    destroyed = true;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (!IsWindow(hwnd)) { destroyed = true; break; }
            }
            if (destroyed) break;

            if (closeRequested_.load() && fadeStartMs < 0)
                fadeStartMs = clock.elapsed();

            if (fadeStartMs >= 0) {
                // ~200 ms fade — SourceConstantAlpha is compositor-side,
                // so this is smooth regardless of the UI thread.
                const double t = double(clock.elapsed() - fadeStartMs) / 200.0;
                fadeAlpha = int(255.0 * std::clamp(1.0 - t, 0.0, 1.0));
                if (t >= 1.0) {
                    DestroyWindow(hwnd);
                    hwnd = nullptr;
                    break;
                }
            }

            // ---- Render one frame into a premultiplied ARGB QImage ----
            QImage img(wPhys, hPhys, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            {
                QPainter pp(&img);
                pp.setRenderHint(QPainter::Antialiasing, true);
                pp.setRenderHint(QPainter::TextAntialiasing, true);
                pp.scale(double(dpi) / 96.0, double(dpi) / 96.0);
                renderSplashFrame(pp, clock.elapsed(), colors_,
                                  splashStatuses());
            }   // painter flushed before the bits are read

            // QImage (32bpp) rows have no padding beyond the 4-byte
            // alignment the DIB also uses, so the bits map 1:1.
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = wPhys;
            bmi.bmiHeader.biHeight      = -hPhys;      // top-down
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP hbmp = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS,
                                            &bits, nullptr, 0);
            if (hbmp && bits) {
                std::memcpy(bits, img.constBits(),
                            size_t(img.bytesPerLine()) * size_t(hPhys));
                HGDIOBJ old = SelectObject(memDc, hbmp);
                SIZE  size = { wPhys, hPhys };
                POINT src  = { 0, 0 };
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, BYTE(fadeAlpha < 0 ? 0 : fadeAlpha),
                                     AC_SRC_ALPHA };
                UpdateLayeredWindow(hwnd, screenDc, nullptr, &size, memDc,
                                    &src, 0, &bf, ULW_ALPHA);
                SelectObject(memDc, old);
            }
            if (hbmp) DeleteObject(hbmp);

            Sleep(16);
        }

        shown_.store(false);
        if (memDc)    DeleteDC(memDc);
        if (screenDc) ReleaseDC(nullptr, screenDc);

        // Callback — back on the UI thread, after the splash is gone.
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lk(cbMutex_);
            cb = std::move(after_);
            after_ = nullptr;
        }
        if (cb && QCoreApplication::instance())
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                                      std::move(cb), Qt::QueuedConnection);
    }

    void failAndExit() {
        failed_.store(true);
        stateCv_.notify_all();
    }

    void stopAndJoinNative() {
        closeRequested_.store(true);
        if (thread_.joinable())
            thread_.join();   // bounded: fade ≤ 200 ms + one 16 ms tick
    }
#endif // Q_OS_WIN

    void stopAndJoin() {
#ifdef Q_OS_WIN
        stopAndJoinNative();
#endif
        widget_.reset();
    }

    // ---- shared state ----------------------------------------------------
    Mode                    mode_ = Mode::None;
    SplashThemeColors       colors_;   // classic navy defaults live in the widget
    std::unique_ptr<SplashOverlay> widget_;

#ifdef Q_OS_WIN
    std::thread             thread_;
    std::mutex              stateMutex_;
    std::condition_variable stateCv_;
#endif
    std::atomic<bool>       shown_{false};
    std::atomic<bool>       failed_{false};
    std::atomic<bool>       closeRequested_{false};
    std::mutex              cbMutex_;
    std::function<void()>   after_;
};

} // namespace DocuSearch
