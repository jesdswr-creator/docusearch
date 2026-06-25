// ============================================================
// QueryParser.cpp
// ============================================================

#include "QueryParser.h"
#include "../core/StringUtils.h"

#include <QRegularExpression>
#include <QStringList>

namespace DocuSearch {

ParsedQuery QueryParser::parse(const QString& raw) {
    ParsedQuery q;
    q.valid = true;

    QString s = raw.trimmed();
    if (s.isEmpty()) return q;

    // Tokenize while respecting quotes and field:value pairs
    QStringList ftsTokens;
    QList<QPair<QString, QString>> fields;

    int i = 0;
    const int n = s.size();
    while (i < n) {
        // Skip whitespace
        while (i < n && s[i].isSpace()) ++i;
        if (i >= n) break;

        // Field:value?
        static const QRegularExpression fieldRe("^(\\w+):");
        auto m = fieldRe.match(s.mid(i));
        if (m.hasMatch()) {
            const QString field = m.captured(1).toLower();
            i += m.capturedLength();
            // Value can be quoted or unquoted
            QString val;
            if (i < n && s[i] == '"') {
                ++i;
                int start = i;
                while (i < n && s[i] != '"') ++i;
                val = s.mid(start, i - start);
                if (i < n) ++i; // consume closing quote
            } else {
                int start = i;
                while (i < n && !s[i].isSpace()) ++i;
                val = s.mid(start, i - start);
            }
            fields.append({field, val});
            continue;
        }

        // Quoted phrase
        if (s[i] == '"') {
            ++i;
            int start = i;
            while (i < n && s[i] != '"') ++i;
            QString phrase = s.mid(start, i - start).trimmed();
            if (i < n) ++i;
            if (!phrase.isEmpty()) {
                // FTS5 phrase syntax: "phrase"
                ftsTokens.append(Utils::fts5Quote(phrase));
            }
            continue;
        }

        // Bare word - could be term or AND/OR/NOT
        int start = i;
        while (i < n && !s[i].isSpace()) ++i;
        QString tok = s.mid(start, i - start);
        if (tok.isEmpty()) continue;

        // Boolean operators (case-sensitive)
        if (tok == "AND" || tok == "OR" || tok == "NOT") {
            // Pass through to FTS5
            ftsTokens.append(tok);
        } else if (tok.startsWith('-')) {
            // Exclusion - convert to FTS5 NOT syntax
            ftsTokens.append("NOT");
            ftsTokens.append(Utils::fts5Quote(tok.mid(1)));
        } else if (tok.contains('*')) {
            // Prefix wildcard - FTS5 supports "prefix*"
            ftsTokens.append(tok);  // bare token with * works in FTS5
        } else {
            ftsTokens.append(Utils::fts5Quote(tok));
        }
    }

    // Build FTS5 query string
    q.ftsQuery = ftsTokens.join(" ");

    // Apply field filters
    for (const auto& f : fields) {
        const QString& field = f.first;
        const QString& val   = f.second;
        if (field == "type" || field == "ext") {
            q.typeFilter = val.toLower();
        } else if (field == "folder" || field == "path") {
            q.folderFilter = val;
        } else if (field == "date" || field == "year") {
            q.dateFilter = val;
        } else if (field == "size") {
            // "size:>10MB" or "size:1MB-10MB"
            if (val.startsWith(">")) q.sizeMin = val.mid(1);
            else if (val.startsWith("<")) q.sizeMax = val.mid(1);
            else q.sizeMin = val;
        } else if (field == "favorite" || field == "fav") {
            q.favoritesOnly = (val == "1" || val.toLower() == "true" || val.isEmpty());
        } else if (field == "ocr") {
            q.ocrOnly = true;
        } else if (field == "tag") {
            // Tag filtering is applied as a sub-join against the Tags table in
            // SearchEngine (Tags are NOT in the FTS5 index - they live in a
            // separate normalized table with UNIQUE(file_id, tag)).
            if (!val.isEmpty()) q.tagFilter = val;
        } else {
            // Unknown field - treat as a regular term
            if (!q.ftsQuery.isEmpty()) q.ftsQuery += " ";
            q.ftsQuery += Utils::fts5Quote(val);
        }
    }

    return q;
}

} // namespace DocuSearch
