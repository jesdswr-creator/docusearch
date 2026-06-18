#pragma once

#include <QObject>
#include "../core/Types.h"
#include "../core/Config.h"

namespace DocuSearch {

// Higher-level settings manager that bridges UI ↔ Config and emits change
// notifications to interested subsystems (indexer, OCR, watcher, etc.)
class SettingsManager : public QObject {
    Q_OBJECT
public:
    static SettingsManager& instance();

    AppSettings current() const { return Config::instance().load(); }
    void apply(const AppSettings& s);

signals:
    void settingsChanged(const AppSettings& s);
};

} // namespace DocuSearch
