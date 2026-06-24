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
#include <QPainter>
#include <QPixmap>
#include <QTimer>

using namespace DocuSearch;

// Create a branded splash screen pixmap (procedural - no external image needed)
static QPixmap createSplashPixmap() {
    const int w = 500, h = 280;
    QPixmap pix(w, h);
    pix.fill(QColor(32, 32, 32));  // dark background

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Accent bar at top
    p.fillRect(0, 0, w, 6, QColor(0, 120, 212));

    // Title
    p.setPen(QColor(255, 255, 255));
    QFont titleFont("Segoe UI", 28, QFont::Bold);
    p.setFont(titleFont);
    p.drawText(pix.rect().adjusted(0, 40, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "DocuSearch");

    // Version
    p.setPen(QColor(150, 150, 150));
    QFont verFont("Segoe UI", 12);
    p.setFont(verFont);
    p.drawText(pix.rect().adjusted(0, 80, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               QString("Version %1").arg(Constants::kAppVersion));

    // Subtitle
    p.setPen(QColor(180, 180, 180));
    QFont subFont("Segoe UI", 11);
    p.setFont(subFont);
    p.drawText(pix.rect().adjusted(0, 110, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               "Offline Intelligent Document Search");

    // Made with love by MinZ
    p.setPen(QColor(200, 100, 100));
    QFont heartFont("Segoe UI", 12, QFont::Bold);
    p.setFont(heartFont);
    p.drawText(pix.rect().adjusted(0, 200, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               "\xE2\x99\xA5 Made with love by MinZ");

    // Tech stack
    p.setPen(QColor(120, 120, 120));
    QFont techFont("Segoe UI", 9);
    p.setFont(techFont);
    p.drawText(pix.rect().adjusted(0, 230, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               "C++20  \xC2\xB7  Qt 6  \xC2\xB7  SQLite + FTS5  \xC2\xB7  Completely Offline");

    // Accent bar at bottom
    p.fillRect(0, h - 4, w, 4, QColor(0, 120, 212));

    return pix;
}

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(Constants::kAppName);
    app.setApplicationVersion(Constants::kAppVersion);
    app.setOrganizationName(Constants::kOrgName);
    app.setOrganizationDomain(Constants::kOrgDomain);

    // Set app icon
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/DocuSearch-256.png")));

    // Fusion style + Win11-inspired palette
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

    // Splash screen with MinZ branding
    QSplashScreen splash(createSplashPixmap());
    splash.show();
    app.processEvents();

    // Initialize logger
    auto& log = DocuSearch::Logger::instance();
    log.init(DocuSearch::Config::instance().logDir(), DocuSearch::LogLevel::Info);

    // Build main window
    DocuSearch::MainWindow w;

    // Show window and close splash after 1.5 seconds
    QTimer::singleShot(1500, [&]() {
        w.show();
        splash.finish(&w);
    });

    return app.exec();
}
