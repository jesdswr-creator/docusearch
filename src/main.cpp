// ============================================================
// main.cpp — Clean, minimal entry point for DocuSearch
// ============================================================

#include "core/Config.h"
#include "core/Constants.h"
#include "core/Logger.h"
#include "core/SehTranslator.h"
#include "core/CrashHandler.h"
#include "core/SystemProfile.h"
#include "core/TierConfig.h"
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

    // Detect system profile (constructs the process-wide singleton on
    // this thread so Database::open on the splash worker can read it
    // without a first-call race).
    const SystemProfile profile = SystemProfiler::instance()->profile();
    DS_INFO("App", QString("Detected system: %1").arg(SystemProfiler::tierName(profile.tier)));

    const TierConfig tierCfg = TierConfigManager::getConfig(profile.tier);
    const int maxThreads = tierCfg.extractionWorkers + 2;
    QThreadPool::globalInstance()->setMaxThreadCount(maxThreads);
    QThreadPool::globalInstance()->setStackSize(tierCfg.threadStackSize * 1024);

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::Round);

    QApplication app(argc, argv);
    app.setApplicationName(Constants::kAppName);
    app.setApplicationVersion(Constants::kAppVersion);
    app.setOrganizationName(Constants::kOrgName);
    app.setOrganizationDomain(Constants::kOrgDomain);

    // ── v1.7.8: SINGLE-INSTANCE GUARD ──
    auto* instanceGuard =
        new QSharedMemory(QStringLiteral("docusearch-single-instance"));
    bool anotherInstance = false;
    if (instanceGuard->attach(QSharedMemory::ReadOnly)) {
        anotherInstance = true;
        instanceGuard->detach();
    } else if (!instanceGuard->create(1)) {
        anotherInstance = true;
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
#ifdef QT_NO_DEBUG
        DocuSearch::LogLevel::Info,
#else
        DocuSearch::LogLevel::Debug,
#endif
        /*mirrorToStderr=*/false);

    DS_INFO("App", QString("Detected system: %1 | %2 GB RAM | %3 cores%4")
                      .arg(SystemProfiler::tierName(profile.tier))
                      .arg(profile.totalRAM / (1LL << 30))
                      .arg(profile.cpuCores)
                      .arg(profile.hasSSD ? QStringLiteral(" | SSD")
                                          : QStringLiteral(" | HDD")));

    app.setWindowIcon(QIcon(":/icons/DocuSearch-256.png"));
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // ── Splash screen: show IMMEDIATELY ──
    const AppSettings saved = DocuSearch::Config::instance().load();
    DocuSearch::NativeSplash splash;
    {
        DocuSearch::SplashThemeColors c;
        if (saved.darkMode) {
            c.cardTop    = QColor("#1b212b");
            c.cardBottom = QColor("#2b3547");
            c.title      = QColor("#e8edf5");
            c.muted      = QColor("#97a3b8");
            c.caption    = QColor("#cbd5e1");
            c.accent     = QColor("#4d8df6");
            c.slot       = QColor(255, 255, 255, 28);
            c.shadow     = QColor(2, 8, 20, 120);
        } else {
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
    app.processEvents();

    // ── Theme-aware startup palette ──
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
    std::unique_ptr<DocuSearch::MainWindow> w;
    QElapsedTimer splashClock;
    splashClock.start();
    constexpr int kMinSplashMs = 1000;

    bool windowShown = false;
    auto showWindowAndDropSplash = [&]() {
        windowShown = true;
        splash.fadeOutAndClose([&w]() {
            if (w) w->show();
        });
    };

    // ── v1.7.20: PRE-OPEN THE DATABASE ON A BACKGROUND THREAD ──
    struct StartupDb {
        std::unique_ptr<DocuSearch::Database> db;
        QString error;
    };
    auto startup = std::make_shared<StartupDb>();
    startup->db = std::make_unique<DocuSearch::Database>();
    const QString dbPath = DocuSearch::Config::instance().dbPath();

    auto* dbWatch = new QFutureWatcher<void>(&app);
    QObject::connect(dbWatch, &QFutureWatcher<void>::finished, &app,
        [&app, &w, &splash, &splashClock, &showWindowAndDropSplash, startup, dbWatch]() {
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
    QTimer::singleShot(30 * 1000, &app, [&splash, &windowShown]() {
        if (!windowShown) {
            DS_WARN("App", "Main window not visible 30 s after launch — "
                           "dropping the splash so nothing hides behind it.");
            splash.fadeOutAndClose();
        }
    });

    return app.exec();
}
