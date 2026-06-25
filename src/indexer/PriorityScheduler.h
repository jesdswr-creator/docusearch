#pragma once

// ============================================================
// PriorityScheduler.h - Determines file processing order
// ============================================================

#include <QObject>
#include <QString>
#include "../core/Types.h"

namespace DocuSearch {

// Computes the IndexPriority band for a file given its modification time
// and extension. Recent files (P1) are processed first so the user gets
// fast search on what they're actively working on.
class PriorityScheduler {
public:
    static IndexPriority compute(const QDateTime& modifiedDate,
                                 const QString& extension);
};

} // namespace DocuSearch
