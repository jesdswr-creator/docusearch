// ============================================================
// main.cpp — Clean, minimal entry point for DocuSearch
// ============================================================
// Stripped down to the bare minimum to get a WORKING window.
// No diagnostic console, no watchdog, no heavy logging.
// Just: QApplication → MainWindow → show → exec.
// ============================================================

#include "../core/Config.h"
#include "../core/Constants.h"
#include "../core/Logger.h"
#include "../core/SehTranslator.h"
#include "../core/CrashHandler.h"
#include "ui/MainWindow.h"
#include "ui/SplashOverlay.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QIcon>
#include <QThread>
#include <QThreadPool>

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
    DocuSearch::SplashOverlay splash;
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

    // ── Construct MainWindow (this is the slow part) ──
    DocuSearch::MainWindow w;

    // Show the real window first, then drop the splash in the same
    // event-loop turn — no desktop gap, no lingering splash frame.
    w.show();
    splash.close();

    return app.exec();
}
