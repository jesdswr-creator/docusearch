#include "SettingsManager.h"

namespace DocuSearch {

SettingsManager& SettingsManager::instance() {
    static SettingsManager inst;
    return inst;
}

void SettingsManager::apply(const AppSettings& s) {
    Config::instance().save(s);
    emit settingsChanged(s);
}

} // namespace DocuSearch
