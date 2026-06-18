// ============================================================
// PriorityScheduler.cpp
// ============================================================

#include "PriorityScheduler.h"
#include "../core/Constants.h"

#include <QDateTime>
#include <QStringList>

namespace DocuSearch {

IndexPriority PriorityScheduler::compute(const QDateTime& modifiedDate,
                                         const QString& extension) {
    static const QStringList kArchives = {"zip", "rar", "7z", "tar", "gz", "bz2"};
    if (kArchives.contains(extension.toLower()))
        return IndexPriority::P4_Archives;

    const qint64 days = modifiedDate.daysTo(QDateTime::currentDateTime());
    if (days <= Constants::kPriority1Days)  return IndexPriority::P1_Recent30Days;
    if (days <= Constants::kPriority2Days)  return IndexPriority::P2_LastYear;
    return IndexPriority::P3_Older;
}

} // namespace DocuSearch
