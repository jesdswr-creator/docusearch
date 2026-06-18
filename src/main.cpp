// ============================================================
// main.cpp — Entry point for DocuSearch (heavy-diagnostic build)
// ============================================================
//
// FIXES IN THIS VERSION:
//   1. AllocConsole() + freopen so stderr actually works when
//      launched from a .bat (GUI apps have no console by default)
//   2. Watchdog QTimer to detect if the event loop is running
//   3. Even more granular logging around every step
// ============================================================

#include "../core/Config.h"
#include "../core/Constants.h"
#include "../core/Logger.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>
#include <QStyleFactory>
#include <QSplashScreen>
#include <QTimer>
#include <QPixmap>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QPluginLoader>
#include <QWindow>
#include <QScreen>
#include <QMenuBar>
#include <QWidget>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
#  include <shobjidl.h>
#  include <io.h>
#  include <fcntl.h>
#endif

#ifdef Q_OS_WIN
#  include "win/JumpList.h"
#endif

using namespace DocuSearch;

// ============================================================
// Synchronous diagnostic logger
// ============================================================
namespace {

QFile   g_exeLog;
QFile*  g_appDataLog = nullptr;
bool    g_haveConsole = false;

void openDiagLogs(const QString& exeDir) {
    QString exeLogPath = exeDir + "/startup.log";
    g_exeLog.setFileName(exeLogPath);
    g_exeLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataDir.isEmpty()) {
        appDataDir = QDir::homePath() + "/AppData/Roaming/DocuSearch";
    }
    QDir().mkpath(appDataDir);
    g_appDataLog = new QFile(appDataDir + "/startup.log");
    g_appDataLog->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void diag(const QString& msg) {
    QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
                   + "  " + msg + "\n";
    QByteArray utf8 = line.toUtf8();

    if (g_exeLog.isOpen()) {
        g_exeLog.write(utf8);
        g_exeLog.flush();
    }
    if (g_appDataLog && g_appDataLog->isOpen()) {
        g_appDataLog->write(utf8);
        g_appDataLog->flush();
    }
    if (g_haveConsole) {
        fprintf(stderr, "%s", utf8.constData());
        fflush(stderr);
    }
}

// Attach to parent console (if launched from cmd/bat) or allocate a new one
void attachConsole() {
#ifdef Q_OS_WIN
    // Try to attach to the parent process's console first
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        g_haveConsole = true;
        // Redirect stdout/stderr to the attached console
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
        // Set unbuffered
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        return;
    }
    // No parent console — allocate our own so we can see output
    if (AllocConsole()) {
        g_haveConsole = true;
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        SetConsoleTitleW(L"DocuSearch - Diagnostic Console");
    }
#endif
}

} // namespace

static void qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    QString tag;
    switch (type) {
    case QtDebugMsg:    tag = "QT_DEBUG";   break;
    case QtInfoMsg:     tag = "QT_INFO";    break;
    case QtWarningMsg:  tag = "QT_WARNING"; break;
    case QtCriticalMsg: tag = "QT_CRITICAL";break;
    case QtFatalMsg:    tag = "QT_FATAL";   break;
    }
    diag(QString("[%1] %2").arg(tag, msg));
}

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[]) {
    // Attach to console FIRST so all subsequent output is visible
    attachConsole();

    QString exePath = QString::fromLocal8Bit(argv[0]);
    QString exeDir = QFileInfo(exePath).absolutePath();
    if (exeDir.isEmpty()) exeDir = QDir::currentPath();
    openDiagLogs(exeDir);

    diag("");
    diag("============================================================");
    diag("DocuSearch starting up");
    diag(QString("  exe path: %1").arg(exePath));
    diag(QString("  exe dir : %1").arg(exeDir));
    diag(QString("  cwd     : %1").arg(QDir::currentPath()));
    diag(QString("  argc    : %1").arg(argc));
    for (int i = 0; i < argc; ++i) {
        diag(QString("  argv[%1] : %2").arg(i).arg(QString::fromLocal8Bit(argv[i])));
    }

    diag("");
    diag("=== Critical file check ===");
    auto checkFile = [&](const QString& name) {
        QString path = exeDir + "/" + name;
        bool exists = QFile::exists(path);
        diag(QString("  [%1] %2").arg(exists ? "OK " : "MISS").arg(name));
        return exists;
    };
    checkFile("DocuSearch.exe");
    checkFile("qt.conf");
    checkFile("Qt6Core.dll");
    checkFile("Qt6Gui.dll");
    checkFile("Qt6Widgets.dll");
    checkFile("platforms/qwindows.dll");
    checkFile("libgcc_s_seh-1.dll");
    checkFile("libstdc++-6.dll");
    checkFile("libwinpthread-1.dll");

    try {
        diag("");
        diag("=== Step 1: Set HighDPI policy ===");
        QApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

        diag("=== Step 2: Construct QApplication ===");
        QApplication app(argc, argv);
        app.setApplicationName(Constants::kAppName);
        app.setApplicationVersion(Constants::kAppVersion);
        app.setOrganizationName(Constants::kOrgName);
        app.setOrganizationDomain(Constants::kOrgDomain);
        diag("  QApplication constructed OK");

        diag("=== Step 3: Install Qt message handler ===");
        qInstallMessageHandler(qtMessageHandler);

        diag("=== Step 4: Set application icon ===");
        app.setWindowIcon(QIcon(QStringLiteral(":/icons/DocuSearch-256.png")));

        diag("=== Step 5: Set Fusion style ===");
        QApplication::setStyle(QStyleFactory::create("Fusion"));

        diag("=== Step 6: List Qt plugin search paths ===");
        QStringList pluginPaths = QCoreApplication::libraryPaths();
        for (const auto& p : pluginPaths) {
            diag(QString("  libraryPath: %1").arg(p));
        }
        diag(QString("  QT_PLUGIN_PATH env: '%1'").arg(QString::fromLocal8Bit(qgetenv("QT_PLUGIN_PATH"))));

        diag("=== Step 7: Try to load qwindows plugin manually ===");
        QString qwindowsPath = exeDir + "/platforms/qwindows.dll";
        diag(QString("  qwindows.dll path: %1").arg(qwindowsPath));
        diag(QString("  qwindows.dll exists: %1").arg(QFile::exists(qwindowsPath)));
        QPluginLoader loader(qwindowsPath);
        if (loader.load()) {
            diag("  qwindows.dll loaded OK as Qt plugin");
        } else {
            diag(QString("  qwindows.dll FAILED to load: %1").arg(loader.errorString()));
        }

        diag("=== Step 8: Load splash pixmap ===");
        QPixmap splashPix(QStringLiteral(":/images/splash.png"));
        diag(QString("  splash pixmap isNull=%1 size=%2x%3")
             .arg(splashPix.isNull()).arg(splashPix.width()).arg(splashPix.height()));

        diag("=== Step 9: Construct QSplashScreen ===");
        QSplashScreen splash(splashPix);
        splash.setStyleSheet("color: #444;");
        splash.showMessage(QStringLiteral("Loading DocuSearch..."),
                           Qt::AlignBottom | Qt::AlignHCenter);

        diag("=== Step 10: splash.show() ===");
        splash.show();
        diag("  splash.show() returned OK");

        diag("=== Step 11: app.processEvents() ===");
        app.processEvents();
        diag("  processEvents() returned OK");

        diag("=== Step 12: Load settings ===");
        auto settings = DocuSearch::Config::instance().load();
        diag(QString("  darkMode=%1").arg(settings.darkMode));

        // Override dark mode via env var for diagnostic: DOCUSEARCH_FORCE_LIGHT=1
        if (qgetenv("DOCUSEARCH_FORCE_LIGHT") == "1") {
            diag("  DOCUSEARCH_FORCE_LIGHT=1 — forcing LIGHT theme");
            settings.darkMode = false;
        }

        diag("=== Step 13: Apply theme ===");
        DocuSearch::Theme::apply(settings.darkMode ? DocuSearch::Theme::Mode::Dark
                                                   : DocuSearch::Theme::Mode::Light);
        diag(QString("  Theme applied (darkMode=%1)").arg(settings.darkMode));

        diag("=== Step 14: Initialize Logger ===");
        auto& log = DocuSearch::Logger::instance();
        log.init(DocuSearch::Config::instance().logDir(), DocuSearch::LogLevel::Info);

        diag("=== Step 15: Construct MainWindow ===");
        DocuSearch::MainWindow w;
        diag("  MainWindow constructed OK");

        diag("=== Step 16: w.show() ===");
        w.show();
        diag("  w.show() returned OK");

        // Force the window to the foreground and repaint — fixes "blank window"
        // on Windows 11 where the window is created but not painted.
        diag("=== Step 16b: w.raise() + activateWindow() ===");
        w.raise();
        w.activateWindow();
        w.setWindowState(Qt::WindowNoState);

        diag("=== Step 16c: w.update() + repaint() ===");
        w.update();
        w.repaint();
        QApplication::processEvents();

        diag("=== Step 17: splash.finish() ===");
        splash.finish(&w);
        // Sometimes splash.finish() doesn't actually close the splash — force it
        splash.close();
        diag("  splash.finish() + close() returned OK");

        diag("=== Step 18: Verify window is visible ===");
        diag(QString("  w.isVisible() = %1").arg(w.isVisible()));
        diag(QString("  w.geometry() = %1,%2 %3x%4")
             .arg(w.x()).arg(w.y()).arg(w.width()).arg(w.height()));
        diag(QString("  w.windowHandle() = %1").arg(w.windowHandle() ? "exists" : "NULL"));
        if (w.windowHandle()) {
            diag(QString("  windowHandle->isVisible() = %1").arg(w.windowHandle()->isVisible()));
            diag(QString("  windowHandle->isExposed() = %1").arg(w.windowHandle()->isExposed()));
        }
        // Inspect the central widget tree
        auto* cw = w.centralWidget();
        diag(QString("  centralWidget = %1 (%2)")
             .arg(cw ? "exists" : "NULL")
             .arg(cw ? cw->metaObject()->className() : ""));
        if (cw) {
            diag(QString("  centralWidget->isVisible() = %1").arg(cw->isVisible()));
            diag(QString("  centralWidget->geometry() = %1,%2 %3x%4")
                 .arg(cw->x()).arg(cw->y()).arg(cw->width()).arg(cw->height()));
            diag(QString("  centralWidget children count = %1").arg(cw->children().size()));
        }
        diag(QString("  menuBar = %1").arg(w.menuBar() ? "exists" : "NULL"));
        if (w.menuBar()) {
            diag(QString("  menuBar actions count = %1").arg(w.menuBar()->actions().size()));
        }

#ifdef Q_OS_WIN
        diag("=== Step 19: Jump list (optional) ===");
        try {
            DocuSearch::Win::JumpList jl;
            jl.addTask(QStringLiteral("Open Settings"),   QStringLiteral("--settings"));
            jl.addTask(QStringLiteral("Pause Indexing"),  QStringLiteral("--pause"));
            jl.addTask(QStringLiteral("Resume Indexing"), QStringLiteral("--resume"));
            jl.commit();
            diag("  JumpList committed");
        } catch (...) {
            diag("  JumpList threw (non-fatal)");
        }
#endif

        // Watchdog: log every 2 seconds to prove the event loop is alive
        diag("=== Step 20: Install watchdog timer ===");
        QTimer watchdog;
        watchdog.setInterval(2000);
        int tickCount = 0;
        QObject::connect(&watchdog, &QTimer::timeout, [&]() {
            tickCount++;
            diag(QString("WATCHDOG tick %1 — event loop alive (visible=%2, geometry=%3,%4 %5x%6)")
                 .arg(tickCount)
                 .arg(w.isVisible())
                 .arg(w.x()).arg(w.y()).arg(w.width()).arg(w.height()));
            // Force a repaint every tick — helps if Qt is dropping paint events
            w.repaint();
        });
        watchdog.start();

        diag("=== Step 21: Enter Qt event loop ===");
        int rc = app.exec();
        diag(QString("=== Event loop exited with code %1 ===").arg(rc));

        log.shutdown();
        if (g_appDataLog) { g_appDataLog->flush(); g_appDataLog->close(); }
        g_exeLog.flush();
        g_exeLog.close();
        return rc;

    } catch (const std::exception& e) {
        diag("");
        diag("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        diag(QString("FATAL: std::exception: %1").arg(e.what()));
        diag("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        if (g_appDataLog) g_appDataLog->flush();
        g_exeLog.flush();
        try {
            QMessageBox::critical(nullptr,
                QStringLiteral("DocuSearch - Startup Failed"),
                QStringLiteral("Error: %1\n\nLog: %2/startup.log").arg(e.what(), exeDir));
        } catch (...) {}
        return 1;
    } catch (...) {
        diag("FATAL: unknown exception");
        if (g_appDataLog) g_appDataLog->flush();
        g_exeLog.flush();
        return 1;
    }
}
