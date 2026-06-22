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
#include "ui/MainWindow.h"

#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>

using namespace DocuSearch;

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(Constants::kAppName);
    app.setApplicationVersion(Constants::kAppVersion);
    app.setOrganizationName(Constants::kOrgName);
    app.setOrganizationDomain(Constants::kOrgDomain);

    // Use Fusion style — modern cross-platform look. We DON'T apply any QSS
    // theme (the QSS was causing the blank window). Fusion without QSS gives
    // a clean, modern, native-looking interface.
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // Windows 11-inspired light palette (no QSS - just palette, which Qt
    // renders correctly without breaking).
    QPalette pal;
    // Backgrounds
    pal.setColor(QPalette::Window,          QColor(243, 243, 243));  // window bg
    pal.setColor(QPalette::Base,            QColor(255, 255, 255));  // input bg
    pal.setColor(QPalette::AlternateBase,   QColor(249, 249, 249));  // alt list rows
    // Text
    pal.setColor(QPalette::WindowText,      QColor(32, 32, 32));
    pal.setColor(QPalette::Text,            QColor(32, 32, 32));
    pal.setColor(QPalette::ButtonText,      QColor(32, 32, 32));
    // Buttons
    pal.setColor(QPalette::Button,          QColor(243, 243, 243));
    // Selection (Win11 accent blue)
    pal.setColor(QPalette::Highlight,       QColor(0, 120, 212));
    pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    // Tooltips
    pal.setColor(QPalette::ToolTipBase,     QColor(255, 255, 255));
    pal.setColor(QPalette::ToolTipText,     QColor(32, 32, 32));
    // Disabled state
    pal.setColor(QPalette::Disabled, QPalette::WindowText,  QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::Text,        QColor(160, 160, 160));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,  QColor(160, 160, 160));
    QApplication::setPalette(pal);

    // Don't apply any QSS theme for now — use native Windows styling.
    // We can re-enable themes once the basic window works.
    // auto& log = DocuSearch::Logger::instance();
    // log.init(DocuSearch::Config::instance().logDir(), DocuSearch::LogLevel::Info);

    DocuSearch::MainWindow w;
    w.show();

    return app.exec();
}
