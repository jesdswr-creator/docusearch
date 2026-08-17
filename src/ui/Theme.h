#pragma once

#include <QString>
#include <QColor>

namespace DocuSearch {

// Centralized color palette + theme loader.
class Theme {
public:
    enum class Mode { Dark, Light };

    // Apply the global QPalette + QSS stylesheet for the given mode.
    static void apply(Mode mode);

    // Returns the QSS stylesheet for the given mode.
    static QString stylesheet(Mode mode);

    // Accent color (used by progress bars, selection, etc.).
    static QColor accent(Mode mode);

    // ── Pastel palette accessor (active palette set by MainWindow::applyTheme).
    // These return the currently-active pastel tokens (Lavender / Mint / Peach),
    // so delegates and custom widgets can read them without re-deriving the
    // colors from QPalette::Highlight (which only carries the primary).
    struct PastelPalette {
        QString bg, surface, surface2, field;
        QString border, hover, text, muted;
        QString primary, primaryStrong, primarySoft, primaryBorder;
        QString themeLabel;
        int     index = 0;   // 0=Lavender, 1=Mint, 2=Peach
    };

    // Returns the active pastel palette. Must be called AFTER
    // MainWindow::applyTheme() at least once.
    static const PastelPalette& active();

    // Sets the active palette (called by MainWindow::applyTheme).
    static void setActive(const PastelPalette& p);
};

} // namespace DocuSearch
