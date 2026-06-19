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

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(Constants::kAppName);
    app.setApplicationVersion(Constants::kAppVersion);
    app.setOrganizationName(Constants::kOrgName);
    app.setOrganizationDomain(Constants::kOrgDomain);

    // Use Windows native style — NOT Fusion + QSS.
    // The QSS theme was causing a blank white window.
    QApplication::setStyle(QStyleFactory::create("Windows"));

    // Don't apply any QSS theme for now — use native Windows styling.
    // We can re-enable themes once the basic window works.
    // auto& log = DocuSearch::Logger::instance();
    // log.init(DocuSearch::Config::instance().logDir(), DocuSearch::LogLevel::Info);

    DocuSearch::MainWindow w;
    w.show();

    return app.exec();
}
