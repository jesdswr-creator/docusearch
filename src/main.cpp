// ============================================================
// main.cpp — Clean, minimal entry point for DocuSearch
// ============================================================
// Stripped down to the bare minimum to get a WORKING window.
// No diagnostic console, no watchdog, no heavy logging.
// Just: QApplication → MainWindow → show → exec.
// ============================================================

#include "core/Config.h"
#include "core/Constants.h"
#include "core/Logger.h"
#include "core/SehTranslator.h"
#include "core/CrashHandler.h"
#include "ui/MainWindow.h"
#include "ui/SplashOverlay.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QIcon>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QElapsedTimer>
#include <QSharedMemory>
#include <QMessageBox>
#include <memory>

using namespace DocuSearch;

int main(int argc, char* argv[]) {
    // Install the crash handler FIRST — before anything that might crash.
    DocuSearch::installCrashHandler();
    DocuSearch::installSehTranslator();

    // Phase 12: Limit thread pool for 4GB RAM systems.
    const int cores = QThread::idealThreadCount();
    const int maxThreads = (cores <= 2) ? 2 : std::min(cores - 1, 4);
    QThreadPool::globalInstance()->setMaxThreadCount(maxThreads);
    QThreadPool::globalInstance()->setStackSize(16 * 1024 * 1024);

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::Round);

    QApplication app(argc, argv);
    app.setApplicationName(Constants::kAppName);
    app.setApplicationVersion(Constants::kAppVersion);
    app.setOrganizationName(Constants::kOrgName);
    app.setOrganizationDomain(Constants::kOrgDomain);

    // ── v1.7.8: SINGLE-INSTANCE GUARD ──
    // Two instances share one SQLite database. The old instance holds
    // write locks during scans; a second copy launched on top of it (an
    // easy accident when installing an upgrade, because the previous
    // version keeps running) then stalls or errors during startup. This
    // runs BEFORE the splash exists, so the message below can never be
    // hidden behind it. The guard object is intentionally leaked for the
    // process lifetime: on Windows the segment dies with the process, so
    // a crashed/closed instance never blocks the next launch.
    auto* instanceGuard =
        new QSharedMemory(QStringLiteral("docusearch-single-instance"));
    bool anotherInstance = false;
    if (instanceGuard->attach(QSharedMemory::ReadOnly)) {
        anotherInstance = true;
        instanceGuard->detach();
    } else if (!instanceGuard->create(1)) {
        anotherInstance = true;   // create failed = someone owns it
    }
    if (anotherInstance) {
        QMessageBox::warning(nullptr, QStringLiteral("DocuSearch"),
            QStringLiteral("DocuSearch is already running.\n\n"
                           "Close the other DocuSearch window (it may be "
                           "minimized to the taskbar) and try again."));
        delete instanceGuard;
        return 0;
    }

    // Initialize the logger AFTER QApplication.
    DocuSearch::Logger::instance().init(
        DocuSearch::Config::instance().logDir(),
        DocuSearch::LogLevel::Debug,
        /*mirrorToStderr=*/false);

    app.setWindowIcon(QIcon(":/icons/DocuSearch-256.png"));
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // ── Splash screen: show IMMEDIATELY so the user sees something ──
    // The MainWindow constructor takes ~1-3 seconds (DB open, schema
    // migrate, widget creation, QSS parsing, icon loading). Without a
    // splash screen, the user double-clicks the exe and sees nothing
    // for several seconds — feels broken.
    //
    // The splash is DRAWN IN CODE (SplashOverlay.h): no static PNG, so
    // no baked-in white stroke around the card (the old artwork's white
    // outline was visible on any background), crisp at every DPI, and
    // it carries a live indeterminate progress bar + cycling status
    // caption so startup visibly moves.
    //
    // v1.7.5 SMOOTHNESS: MainWindow is now constructed AFTER app.exec()
    // starts (deferred by a 0 ms single-shot). With the event loop live,
    // the splash's 16 ms animation timer actually fires while the heavy
    // constructor runs, so the sliding bar animates at full frame rate
    // instead of the handful of manual processEvents() pumps the old
    // synchronous construction produced. A minimum display time keeps
    // the animation visible even on very fast machines.
    //
    // v1.7.6 THEME MATCH: the splash used a hardcoded navy palette that
    // clashed with the app's theme. It now derives from the SAME tokens
    // as MainWindow::applyTheme() — the card and text follow the active
    // theme's surfaces, and the progress chunk + magnifier use the exact
    // button color (@primary@) of the saved theme. Reading the saved
    // darkMode here also keeps the splash consistent with the fixed
    // theme wiring (the window renders the same theme right after).
    DocuSearch::SplashOverlay splash;
    {
        SplashOverlay::ThemeColors c;
        const AppSettings saved = DocuSearch::Config::instance().load();
        if (saved.darkMode) {
            // Midnight palette — buttons are #4d8df6.
            c.cardTop    = QColor("#1b212b");
            c.cardBottom = QColor("#2b3547");
            c.title      = QColor("#e8edf5");
            c.muted      = QColor("#97a3b8");
            c.caption    = QColor("#cbd5e1");
            c.accent     = QColor("#4d8df6");
            c.slot       = QColor(255, 255, 255, 28);
            c.shadow     = QColor(2, 8, 20, 120);
        } else {
            // Daylight palette — buttons are #2563eb.
            c.cardTop    = QColor("#ffffff");
            c.cardBottom = QColor("#f2f4f8");
            c.title      = QColor("#151f2c");
            c.muted      = QColor("#667188");
            c.caption    = QColor("#3f4b5e");
            c.accent     = QColor("#2563eb");
            c.slot       = QColor(21, 31, 44, 26);
            c.shadow     = QColor(21, 31, 44, 50);
        }
        splash.setThemeColors(c);
    }
    splash.show();
    app.processEvents();  // Force paint the splash immediately

    QPalette pal;
    pal.setColor(QPalette::Window,          QColor(243, 243, 243));
    pal.setColor(QPalette::Base,            QColor(255, 255, 255));
    pal.setColor(QPalette::AlternateBase,   QColor(249, 249, 249));
    pal.setColor(QPalette::WindowText,      QColor(32, 32, 32));
    pal.setColor(QPalette::Text,            QColor(32, 32, 32));
    pal.setColor(QPalette::ButtonText,      QColor(32, 32, 32));
    pal.setColor(QPalette::Button,          QColor(243, 243, 243));
    pal.setColor(QPalette::Highlight,       QColor(0, 120, 212));
    pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    pal.setColor(QPalette::ToolTipBase,     QColor(255, 255, 255));
    pal.setColor(QPalette::ToolTipText,     QColor(32, 32, 32));
    pal.setColor(QPalette::Disabled, QPalette::WindowText,  QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::Text,        QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,  QColor(160, 160, 160));
    QApplication::setPalette(pal);

    // ── Construct MainWindow once the event loop is running ──
    // The window must outlive app.exec(), so it lives here in main()
    // and is created from a 0 ms single-shot. The splash stays visible
    // (and animating) until the window is shown.
    std::unique_ptr<DocuSearch::MainWindow> w;
    QElapsedTimer splashClock;
    splashClock.start();
    constexpr int kMinSplashMs = 1000;  // let the animation breathe on fast machines

    bool windowShown = false;
    auto showWindowAndDropSplash = [&]() {
        windowShown = true;
        if (w) w->show();
        splash.close();
    };

    // ── v1.7.8: SPLASH SAFETY NET ──
    // The splash is an always-on-top window; if anything ever blocks the
    // constructor (a modal error dialog opened behind it, a slow one-time
    // cleanup, a stalled USB/network volume), the user must never be
    // trapped staring at it forever. After 30 s without a visible main
    // window, drop the splash: anything that was hiding behind it becomes
    // visible and clickable, and the main window still shows whenever it
    // is ready.
    QTimer::singleShot(30 * 1000, &app, [&splash, &windowShown]() {
        if (!windowShown) {
            DS_WARN("App", "Main window not visible 30 s after launch — "
                           "dropping the splash so nothing hides behind it.");
            splash.close();
        }
    });

    QTimer::singleShot(0, &app, [&]() {
        try {
            w = std::make_unique<DocuSearch::MainWindow>();
        } catch (...) {
            // Constructor failure (e.g. DB locked) — the ctor shows its
            // own message box; just drop the splash and quit cleanly.
            splash.close();
            QTimer::singleShot(0, &app, []() { QApplication::quit(); });
            return;
        }
        const int remain = kMinSplashMs - static_cast<int>(splashClock.elapsed());
        if (remain > 0) {
            QTimer::singleShot(remain, &app, showWindowAndDropSplash);
        } else {
            showWindowAndDropSplash();
        }
    });

    return app.exec();
}
