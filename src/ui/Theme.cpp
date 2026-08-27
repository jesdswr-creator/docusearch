// ============================================================
// Theme.cpp - theme plumbing for DocuSearch.
//
// Two modes share ONE master stylesheet compiled in as
// :/themes/base.qss (resources/themes/base.qss). MainWindow::applyTheme()
// is the authoritative styling path: it loads that sheet, substitutes
// @token@ palette values, and applies it app-wide along with QPalette.
// Theme::apply()/stylesheet() stay as thin resource-loading fallbacks for
// callers outside MainWindow. The legacy per-mode literal blobs (~31 KB,
// dead code, MSVC C2026 "string too big" risk) are gone.
// ============================================================

#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QFile>

namespace DocuSearch {

namespace {

// The legacy per-mode QSS blobs (~31 KB of raw-string literals that
// duplicated MainWindow::applyTheme and risked MSVC C2026 "string too
// big") were removed. The single master sheet is now compiled in as
// :/themes/base.qss; both modes come from it via @token@ substitution.
QFile::FileError g_lastQssError = QFile::NoError;

QString loadBaseQss() {
    QFile f(QStringLiteral(":/themes/base.qss"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        g_lastQssError = QFile::NoError;
        return QString::fromUtf8(f.readAll());
    }
    g_lastQssError = f.error();
    return {};
}

} // namespace

void Theme::apply(Mode mode) {
    qApp->setStyleSheet(stylesheet(mode));
    // Also tweak the QPalette so non-QSS-aware widgets (system dialogs,
    // tooltips, OS chrome) pick up the right colors.
    QPalette p;
    if (mode == Mode::Dark) {
        p.setColor(QPalette::Window,          QColor("#1c1c1c"));
        p.setColor(QPalette::WindowText,      QColor("#f5f5f5"));
        p.setColor(QPalette::Base,            QColor("#262626"));
        p.setColor(QPalette::AlternateBase,   QColor("#2d2d2d"));
        p.setColor(QPalette::Text,            QColor("#f5f5f5"));
        p.setColor(QPalette::Button,          QColor("#2d2d2d"));
        p.setColor(QPalette::ButtonText,     QColor("#f5f5f5"));
        p.setColor(QPalette::Highlight,       QColor("#4cc2ff"));
        p.setColor(QPalette::HighlightedText, QColor("#003049"));
        p.setColor(QPalette::ToolTipBase,     QColor("#2d2d2d"));
        p.setColor(QPalette::ToolTipText,    QColor("#f5f5f5"));
    } else {
        p.setColor(QPalette::Window,          QColor("#f2f1ee"));
        p.setColor(QPalette::WindowText,      QColor("#1b1b1b"));
        p.setColor(QPalette::Base,            QColor("#ffffff"));
        p.setColor(QPalette::AlternateBase,   QColor("#faf9f7"));
        p.setColor(QPalette::Text,            QColor("#1b1b1b"));
        p.setColor(QPalette::Button,          QColor("#ffffff"));
        p.setColor(QPalette::ButtonText,     QColor("#1b1b1b"));
        p.setColor(QPalette::Highlight,       QColor("#0067c0"));
        p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(QPalette::ToolTipBase,     QColor("#1b1b1b"));
        p.setColor(QPalette::ToolTipText,    QColor("#ffffff"));
    }
    qApp->setPalette(p);
}

QString Theme::stylesheet(Mode mode) {
    // The base sheet is theme-neutral: every mode difference flows in via
    // @token@ substitution done by MainWindow::applyTheme(). For this
    // thin fallback we hand back the unsubstituted sheet (tokens degrade
    // to Qt defaults rather than wrong colors).
    Q_UNUSED(mode);
    return loadBaseQss();
}

QColor Theme::accent(Mode mode) {
    return (mode == Mode::Dark) ? QColor("#4cc2ff") : QColor("#0067c0");
}

namespace {
Theme::PastelPalette g_active;
bool g_activeSet = false;
}

const Theme::PastelPalette& Theme::active() {
    static PastelPalette fallback;
    if (g_activeSet) return g_active;
    // Fallback = Fluent Dark (matches the dark QSS above).
    fallback.bg = "#1c1c1c"; fallback.surface = "#262626"; fallback.surface2 = "#2d2d2d";
    fallback.field = "#2d2d2d"; fallback.border = "rgba(255,255,255,0.085)"; fallback.hover = "rgba(255,255,255,0.055)";
    fallback.text = "#f5f5f5"; fallback.muted = "#a9a9a9";
    fallback.primary = "#4cc2ff"; fallback.primaryStrong = "#67d4ff";
    fallback.primarySoft = "rgba(76,194,255,0.14)"; fallback.primaryBorder = "rgba(76,194,255,0.30)";
    fallback.themeLabel = "Fluent Dark"; fallback.index = 0;
    return fallback;
}

void Theme::setActive(const PastelPalette& p) {
    g_active = p;
    g_activeSet = true;
}

} // namespace DocuSearch
