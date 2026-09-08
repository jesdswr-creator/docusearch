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
#include "database/Database.h"
#include "database/Schema.h"
#include "ui/MainWindow.h"
#include "ui/NativeSplash.h"

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
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
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
    // v1.7.11: Info level in release builds — Debug-level logging in
    // production wrote every icon lookup and model-path probe to disk
    // on all user machines. Debug stays on for developer (non-release)
    // builds. (Old daily logs are also pruned now; see Logger::init.)
    DocuSearch::Logger::instance().init(
        DocuSearch::Config::instance().logDir(),
#ifdef QT_NO_DEBUG
        DocuSearch::LogLevel::Info,
#else
        DocuSearch::LogLevel::Debug,
#endif
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
    // button color (@primary@) of the saved theme.
    // v1.7.18: the saved settings are read ONCE, up front, and reused:
    // the application palette below must already match the saved theme
    // BEFORE MainWindow is constructed, or a dark-mode user sees a light
    // gray first-paint flash behind the (dark) splash — part of the
    // "splash is still buggy" report.
    const AppSettings saved = DocuSearch::Config::instance().load();
    DocuSearch::NativeSplash splash;
    {
        DocuSearch::SplashThemeColors c;
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

    // v1.7.18: THEME-AWARE STARTUP PALETTE. This used to be hardcoded
    // light, so a dark-mode user got a light first paint (before the
    // QSS loads) — a visible white/gray flash at the reveal. The colors
    // mirror Theme::apply(Dark)/Theme::apply(Light) so the pre-QSS paint
    // and the themed paint agree. MainWindow::applyTheme() refines this
    // with the full token stylesheet right after construction.
    QPalette pal;
    if (saved.darkMode) {
        pal.setColor(QPalette::Window,          QColor("#1c1c1c"));
        pal.setColor(QPalette::Base,            QColor("#262626"));
        pal.setColor(QPalette::AlternateBase,   QColor("#2d2d2d"));
        pal.setColor(QPalette::WindowText,      QColor("#f5f5f5"));
        pal.setColor(QPalette::Text,            QColor("#f5f5f5"));
        pal.setColor(QPalette::ButtonText,      QColor("#f5f5f5"));
        pal.setColor(QPalette::Button,          QColor("#2d2d2d"));
        pal.setColor(QPalette::Highlight,       QColor("#4cc2ff"));
        pal.setColor(QPalette::HighlightedText, QColor("#003049"));
        pal.setColor(QPalette::ToolTipBase,     QColor("#2d2d2d"));
        pal.setColor(QPalette::ToolTipText,     QColor("#f5f5f5"));
        pal.setColor(QPalette::Disabled, QPalette::WindowText,  QColor(110, 110, 110));
        pal.setColor(QPalette::Disabled, QPalette::Text,        QColor(110, 110, 110));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText,  QColor(110, 110, 110));
    } else {
        pal.setColor(QPalette::Window,          QColor("#f2f1ee"));
        pal.setColor(QPalette::Base,            QColor("#ffffff"));
        pal.setColor(QPalette::AlternateBase,   QColor("#faf9f7"));
        pal.setColor(QPalette::WindowText,      QColor("#1b1b1b"));
        pal.setColor(QPalette::Text,            QColor("#1b1b1b"));
        pal.setColor(QPalette::ButtonText,      QColor("#1b1b1b"));
        pal.setColor(QPalette::Button,          QColor("#ffffff"));
        pal.setColor(QPalette::Highlight,       QColor("#0067c0"));
        pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        pal.setColor(QPalette::ToolTipBase,     QColor("#1b1b1b"));
        pal.setColor(QPalette::ToolTipText,     QColor("#ffffff"));
        pal.setColor(QPalette::Disabled, QPalette::WindowText,  QColor(160, 160, 160));
        pal.setColor(QPalette::Disabled, QPalette::Text,        QColor(160, 160, 160));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText,  QColor(160, 160, 160));
    }
    QApplication::setPalette(pal);

    // ── Construct MainWindow once the event loop is running ──
    // The window must outlive app.exec(), so it lives here in main().
    // The splash stays visible (and animating) until the window shows.
    std::unique_ptr<DocuSearch::MainWindow> w;
    QElapsedTimer splashClock;
    splashClock.start();
    constexpr int kMinSplashMs = 1000;  // let the animation breathe on fast machines

    bool windowShown = false;
    auto showWindowAndDropSplash = [&]() {
        windowShown = true;
        // v1.7.18: REVEAL ORDER FIX. v1.7.17 showed the main window
        // first and faded the splash over it — the (usually maximized)
        // window popped into existence behind and around the small
        // translucent card, all visible through the splash's transparent
        // margins, plus its own first-paint flash. That overlap WAS the
        // remaining glitch. Now the splash fades out over the desktop
        // while the event loop is idle (a genuinely smooth 200 ms), and
        // ONLY THEN does the main window show. Nothing ever appears or
        // disappears behind a half-transparent overlay.
        splash.fadeOutAndClose([&w]() {
            if (w) w->show();
        });
    };

    // ── v1.7.20: PRE-OPEN THE DATABASE ON A BACKGROUND THREAD ──
    // sqlite open + Schema::initialize/migrate used to run INSIDE the
    // MainWindow constructor, blocking the UI thread (and with it, the
    // splash animation) for hundreds of milliseconds to seconds on big
    // libraries. Now the worker starts immediately (before the splash
    // even paints its first frame — the two overlap), and MainWindow is
    // constructed the moment the future delivers, from the event loop:
    // the UI thread never waits on the database at all. On a healthy
    // launch the DB is usually open by the time the splash minimum
    // display time has elapsed, so the window actually shows SOONER
    // than before; on a slow/locked database the splash keeps animating
    // smoothly instead of freezing.
    //
    // Safety preserved: an open failure surfaces as a message box (then
    // the splash fades and the app quits), and the MainWindow ctor still
    // contains the old inline-open path as a defensive fallback.
    struct StartupDb {
        std::unique_ptr<DocuSearch::Database> db;
        QString error;
    };
    auto startup = std::make_shared<StartupDb>();
    // v1.7.20: create the Database OBJECT on the UI thread (QObject thread
    // affinity follows the creating thread; later adoption as a child of
    // MainWindow must be same-thread). Only the open()/migrate() CALLS run
    // on the worker — they touch no Qt events/signals, so that is safe;
    // the UI thread does not touch the object again until finished().
    startup->db = std::make_unique<DocuSearch::Database>();
    const QString dbPath = DocuSearch::Config::instance().dbPath();

    auto* dbWatch = new QFutureWatcher<void>(&app);
    QObject::connect(dbWatch, &QFutureWatcher<void>::finished, &app,
        [&w, &splash, &splashClock, &showWindowAndDropSplash, startup, dbWatch]() {
            dbWatch->deleteLater();
            if (!startup->db || !startup->db->isOpen()) {
                const QString err = startup->error.isEmpty()
                                        ? QStringLiteral("unknown error")
                                        : startup->error;
                QMessageBox::critical(nullptr, QStringLiteral("Database Error"),
                    QStringLiteral("Failed to open database:\n") + err);
                splash.fadeOutAndClose([]() {
                    QTimer::singleShot(0, qApp,
                                       []() { QApplication::quit(); });
                });
                return;
            }
            try {
                w = std::make_unique<DocuSearch::MainWindow>(
                        std::move(startup->db));
            } catch (...) {
                // Constructor failure (e.g. DB locked) — the ctor shows
                // its own message box; fade the splash out, then quit
                // cleanly (the callback keeps app.exec() alive until the
                // fade ends).
                splash.fadeOutAndClose([]() {
                    QTimer::singleShot(0, qApp,
                                       []() { QApplication::quit(); });
                });
                return;
            }
            const int remain =
                kMinSplashMs - static_cast<int>(splashClock.elapsed());
            if (remain > 0) {
                QTimer::singleShot(remain, &app, showWindowAndDropSplash);
            } else {
                showWindowAndDropSplash();
            }
        });
    dbWatch->setFuture(QtConcurrent::run([startup, dbPath]() {
        QString err;
        if (!startup->db->open(dbPath, &err)) {
            startup->error = err;
            DS_ERROR("App", QString("Background database open failed: %1")
                                .arg(startup->error));
            return;
        }
        if (!DocuSearch::Schema::initialize(*startup->db) ||
            !DocuSearch::Schema::migrate(*startup->db)) {
            startup->error = QStringLiteral("schema initialization failed");
            DS_ERROR("App", "Background schema initialize/migrate failed.");
            return;
        }
        DS_INFO("App", "Database pre-opened on the background thread.");
    }));

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
            splash.fadeOutAndClose();
        }
    });

    return app.exec();
}
