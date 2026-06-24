// ============================================================
// main.cpp — Entry point for DocuSearch
// ============================================================

#include "../core/Config.h"
#include "../core/Constants.h"
#include "../core/Logger.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include <QSplashScreen>
#include <QTimer>
#include <QIcon>

using namespace DocuSearch;

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(Constants::kAppName);
    app.setApplicationVersion(Constants::kAppVersion);
    app.setOrganizationName(Constants::kOrgName);
    app.setOrganizationDomain(Constants::kOrgDomain);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/DocuSearch-256.png")));

    // Fusion style + Win11-inspired light palette
    QApplication::setStyle(QStyleFactory::create("Fusion"));
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

    // Splash screen — keep visible during initialization
    QPixmap splashPix(QStringLiteral(":/images/splash.png"));
    QSplashScreen splash(splashPix);
    splash.show();
    splash.showMessage("Loading...", Qt::AlignBottom | Qt::AlignHCenter, QColor(100, 100, 100));
    app.processEvents();

    // Initialize logger (this takes a moment)
    auto& log = DocuSearch::Logger::instance();
    log.init(DocuSearch::Config::instance().logDir(), DocuSearch::LogLevel::Info);
    app.processEvents();

    // Build main window — this opens the DB, inits schema, builds UI.
    // The window is NOT shown yet (no w.show() here).
    splash.showMessage("Opening database...", Qt::AlignBottom | Qt::AlignHCenter, QColor(100, 100, 100));
    app.processEvents();

    DocuSearch::MainWindow w;

    // Window is fully constructed now. Show it and close splash.
    splash.showMessage("Ready!", Qt::AlignBottom | Qt::AlignHCenter, QColor(0, 120, 212));
    app.processEvents();

    w.show();
    splash.finish(&w);

    return app.exec();
}
