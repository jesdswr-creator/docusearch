#pragma once

// ============================================================
// QueryParser.h - Parse advanced search queries into FTS5 SQL
// ============================================================

#include "../core/Types.h"
#include <QString>

namespace DocuSearch {

// Parses queries like:
//   NOC AND examination
//   "Station Development"
//   type:pdf folder:Railway date:2026
//   "Executive Lounge" -draft
//
// Outputs:
//   - ftsQuery: a string suitable for the WHERE clause of FTS5 MATCH
//   - fieldFilters: structured filters (type, date, folder, size) that we
//     apply as plain SQL on the Files table after FTS5 narrows rows.
struct ParsedQuery {
    QString ftsQuery;        // may be empty if query is only filters
    QString typeFilter;      // e.g., "pdf" (lowercase)
    QString folderFilter;    // substring match on path
    QString dateFilter;      // e.g., "2026" -> year filter
    QString sizeMin;
    QString sizeMax;
    QString tagFilter;       // tag:<name> - file must have this tag
    bool    favoritesOnly = false;
    bool    ocrOnly       = false;
    bool    valid         = true;
    QString errorMessage;
};

class QueryParser {
public:
    static ParsedQuery parse(const QString& raw);
};

} // namespace DocuSearch
