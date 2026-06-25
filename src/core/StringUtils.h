#pragma once

// ============================================================
// StringUtils.h - Common string utilities
// ============================================================

#include <QString>
#include <QStringList>
#include <string>

namespace DocuSearch {
namespace Utils {

// Lowercase + strip accents + collapse whitespace.
// Used for normalising both stored text and query terms.
QString normalize(const QString& s);

// Lowercase ASCII, keep CJK as-is (for indexing/FTS5 tokens).
QString toLowerAscii(const QString& s);

// Truncate text safely around a match for snippet display.
QString snippetAround(const QString& text, const QString& match,
                      int before = 80, int after = 120);

// Sanitize a string so it can be safely embedded as an FTS5 quoted phrase.
QString fts5Quote(const QString& s);

// Strip non-printable / control characters.
QString stripControlChars(const QString& s);

// Convert size in bytes to human-readable form ("12.3 MB").
QString formatFileSize(qint64 bytes);

// Parse "type:pdf date:2026 foo bar" into field/value and free terms.
struct FieldTerm {
    QString field;  // "type", "date", "folder"
    QString value;
};
struct ParsedQueryParts {
    QList<FieldTerm> fields;
    QStringList      freeTerms;   // terms without explicit field
};
ParsedQueryParts splitFieldTerms(const QString& raw);

// Levenshtein distance - used for fuzzy duplicate detection.
int levenshtein(const QString& a, const QString& b);

// Jaccard similarity (0..1) of word sets - used for filename similarity.
double jaccardSimilarity(const QString& a, const QString& b);

// Read a file fully into a QString (UTF-8). Returns empty on failure.
QString readTextFile(const QString& path);

// Encode/decode for safe storage keys.
QString slugify(const QString& s);

}} // namespace DocuSearch::Utils
