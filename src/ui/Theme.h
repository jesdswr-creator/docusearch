#pragma once

#include <QString>
#include <QColor>

namespace DocuSearch {

// Centralized color palette + theme loader.
class Theme {
public:
    enum class Mode { Dark, Light };

    static void apply(Mode mode);

    // Returns the QSS stylesheet for the given mode.
    static QString stylesheet(Mode mode);

    // Accent color (used by progress bars, selection, etc.)
    static QColor accent(Mode mode);
};

} // namespace DocuSearch
