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
    bool    needsOcrOnly  = false;  // is:needs-ocr — files awaiting OCR
    bool    valid         = true;
    QString errorMessage;
    // v1.7.15: the natural-language TEXT of the query — the words and
    // quoted phrases the user actually typed, with every piece of
    // DocuSearch/FTS5 query SYNTAX removed:
    //   • field filters (type:pdf, folder:…, date:…, tag:…)  → dropped
    //   • AND / OR / NOT operators                            → dropped
    //   • -negation words and "NOT <word>"                    → dropped
    //     (embedding a word the user EXCLUDED would pull the query
    //     vector toward exactly the wrong documents)
    //   • wildcard '*' suffixes ("rail*")                     → "rail"
    //   • '+' separators ("A+B")                              → "A B"
    // Stopwords are intentionally KEPT — they are the user's words and
    // the embedding model handles them; the FTS side strips them on its
    // own. May be EMPTY for a pure-filter query ("type:pdf") — callers
    // MUST skip the semantic scan when it is empty, because there are
    // no words to embed. (Embedding the raw query text is what made the
    // AI "ignore the user's words": BGE has never seen "type:" or "-",
    // so "type:pdf NOC -draft" embedded "pdf" and even the excluded
    // word "draft" into the query vector.)
    QString semanticText;
};

class QueryParser {
public:
    static ParsedQuery parse(const QString& raw);
};

} // namespace DocuSearch
