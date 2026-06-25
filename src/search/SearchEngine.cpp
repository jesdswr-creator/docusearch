// ============================================================
// SearchEngine.cpp
// ============================================================

#include "SearchEngine.h"
#include "QueryParser.h"
#include "../database/Database.h"
#include "../database/FileRepository.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"

#include <sqlite3.h>
#include <QDateTime>
#include <QSettings>
#include <QVariant>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

namespace DocuSearch {

namespace {
void bindText(sqlite3_stmt* s, int idx, const QString& v) {
    const QByteArray u = v.toUtf8();
    sqlite3_bind_text(s, idx, u.constData(), u.size(), SQLITE_TRANSIENT);
}
QString colText(sqlite3_stmt* s, int idx) {
    const unsigned char* t = sqlite3_column_text(s, idx);
    return t ? QString::fromUtf8(reinterpret_cast<const char*>(t)) : QString();
}

// Escape a string for use in a LIKE ? ESCAPE '\\' clause.
QString likeEscape(const QString& s) {
    QString out = s;
    out.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
    return out;
}
} // namespace

SearchEngine::SearchEngine(Database& db, FileRepository& repo, QObject* parent)
    : QObject(parent), db_(db), repo_(repo) {}

QStringList SearchEngine::splitSearchWords(const QString& raw) {
    // 1) Remove field: filters (e.g., type:pdf, folder:"My Docs").
    static const QRegularExpression fieldRe("(\\w+):(\"[^\"]+\"|\\S+)");
    QString s = raw;
    s.remove(fieldRe);
    // 2) Replace '+' with space so A+B is treated the same as "A B".
    s.replace('+', ' ');
    // 3) Tokenize respecting quoted phrases.
    QStringList words;
    int i = 0;
    const int n = s.size();
    while (i < n) {
        while (i < n && s[i].isSpace()) ++i;
        if (i >= n) break;
        if (s[i] == '"') {
            // Quoted phrase — keep contents as a single token.
            ++i;
            int start = i;
            while (i < n && s[i] != '"') ++i;
            QString phrase = s.mid(start, i - start).trimmed();
            if (!phrase.isEmpty()) words.append(phrase);
            if (i < n) ++i;  // consume closing quote
        } else {
            int start = i;
            while (i < n && !s[i].isSpace()) ++i;
            QString tok = s.mid(start, i - start).trimmed();
            if (!tok.isEmpty()) words.append(tok);
        }
    }
    // FTS5 boolean operators don't apply to filename LIKE matching — drop them.
    words.removeAll("AND");
    words.removeAll("OR");
    words.removeAll("NOT");
    return words;
}

QList<SearchHit> SearchEngine::search(const QString& rawQuery, int limit) {
    QList<SearchHit> results;
    sqlite3* raw = db_.raw();
    if (!raw) return results;

    const ParsedQuery q = QueryParser::parse(rawQuery);
    const QStringList words = splitSearchWords(rawQuery);

    // ---------------------------------------------------------------
    // Filename search — always run. Generates one LIKE ? per word,
    // all ANDed, so "A B" and "A+B" both match filenames containing
    // both words in any order.
    // ---------------------------------------------------------------
    QList<SearchHit> filenameHits;
    {
        QString sql = "SELECT id, path, filename, extension, size, "
                      "modified_date, is_favorite "
                      "FROM Files WHERE 1=1 ";
        for (int i = 0; i < words.size(); ++i) {
            sql += " AND filename LIKE ? ESCAPE '\\' ";
        }
        if (!q.typeFilter.isEmpty())   sql += " AND extension = ? ";
        if (!q.folderFilter.isEmpty()) sql += " AND path LIKE ? ESCAPE '\\' ";
        if (q.favoritesOnly)          sql += " AND is_favorite = 1 ";
        if (!q.tagFilter.isEmpty())   sql += " AND id IN (SELECT file_id FROM Tags WHERE tag = ?) ";
        sql += " ORDER BY modified_date DESC LIMIT " + QString::number(limit) + ";";

        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) == SQLITE_OK) {
            int idx = 1;
            for (const auto& w : words) {
                bindText(s, idx++, "%" + likeEscape(w) + "%");
            }
            if (!q.typeFilter.isEmpty())   bindText(s, idx++, q.typeFilter);
            if (!q.folderFilter.isEmpty()) bindText(s, idx++, "%" + likeEscape(q.folderFilter) + "%");
            if (!q.tagFilter.isEmpty())    bindText(s, idx++, q.tagFilter);
            while (sqlite3_step(s) == SQLITE_ROW) {
                SearchHit h;
                h.fileId       = sqlite3_column_int64(s, 0);
                h.path         = colText(s, 1);
                h.filename     = colText(s, 2);
                h.extension    = colText(s, 3);
                h.size         = sqlite3_column_int64(s, 4);
                h.modifiedDate = QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(s, 5));
                h.isFavorite   = sqlite3_column_int(s, 6) != 0;
                filenameHits.append(h);
            }
            sqlite3_finalize(s);
        } else {
            DS_WARN("Search", QString("Filename query prepare failed: %1")
                                .arg(sqlite3_errmsg(raw)));
        }
    }

    // If there's no FTS5 query, we're done — return filename hits.
    if (q.ftsQuery.isEmpty()) {
        return filenameHits;
    }

    // ---------------------------------------------------------------
    // FTS5 content search — use empty snippet markers ('', '') so
    // results don't contain HTML like <b>…</b>.
    // ---------------------------------------------------------------
    QList<SearchHit> ftsHits;
    try {
        QString sql = "SELECT f.id, f.path, f.filename, f.extension, f.size, "
                      "f.modified_date, f.is_favorite, "
                      "snippet(SearchIndex, 1, '', '', '...', 16) AS snip, "
                      "bm25(SearchIndex) AS score "
                      "FROM SearchIndex s JOIN Files f ON f.id = s.file_id "
                      "WHERE SearchIndex MATCH ? ";
        if (!q.typeFilter.isEmpty())   sql += " AND f.extension = ? ";
        if (!q.folderFilter.isEmpty()) sql += " AND f.path LIKE ? ESCAPE '\\' ";
        if (q.favoritesOnly)          sql += " AND f.is_favorite = 1 ";
        if (!q.tagFilter.isEmpty())   sql += " AND f.id IN (SELECT file_id FROM Tags WHERE tag = ?) ";
        sql += " ORDER BY score ASC LIMIT " + QString::number(limit) + ";";

        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) == SQLITE_OK) {
            int idx = 1;
            const QByteArray fts = q.ftsQuery.toUtf8();
            sqlite3_bind_text(s, idx++, fts.constData(), fts.size(), SQLITE_TRANSIENT);
            if (!q.typeFilter.isEmpty())   bindText(s, idx++, q.typeFilter);
            if (!q.folderFilter.isEmpty()) bindText(s, idx++, "%" + likeEscape(q.folderFilter) + "%");
            if (!q.tagFilter.isEmpty())    bindText(s, idx++, q.tagFilter);

            while (sqlite3_step(s) == SQLITE_ROW) {
                SearchHit h;
                h.fileId       = sqlite3_column_int64(s, 0);
                h.path         = colText(s, 1);
                h.filename     = colText(s, 2);
                h.extension    = colText(s, 3);
                h.size         = sqlite3_column_int64(s, 4);
                h.modifiedDate = QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(s, 5));
                h.isFavorite   = sqlite3_column_int(s, 6) != 0;
                h.snippet      = colText(s, 7);
                h.score        = static_cast<float>(sqlite3_column_double(s, 8));
                ftsHits.append(h);
            }
            sqlite3_finalize(s);
        } else {
            // FTS5 query parse error — fall back silently to filename hits.
            DS_WARN("Search", QString("FTS prepare failed, filename-only: %1")
                                .arg(sqlite3_errmsg(raw)));
        }
    } catch (...) {
        DS_WARN("Search", "FTS search threw exception, returning filename hits only");
    }

    // ---------------------------------------------------------------
    // Merge: content matches (with snippet) first, then filename
    // matches by date. Dedupe by fileId.
    // ---------------------------------------------------------------
    QSet<qint64> seen;
    seen.reserve(ftsHits.size() + filenameHits.size());
    for (const auto& h : ftsHits) {
        if (seen.contains(h.fileId)) continue;
        seen.insert(h.fileId);
        results.append(h);
    }
    // filenameHits is already ordered by modified_date DESC from SQL.
    for (const auto& h : filenameHits) {
        if (seen.contains(h.fileId)) continue;
        seen.insert(h.fileId);
        results.append(h);
    }
    if (results.size() > limit) results = results.mid(0, limit);
    return results;
}

QList<SearchHit> SearchEngine::searchByFilename(const QString& term, int limit) {
    QList<SearchHit> results;
    sqlite3* raw = db_.raw();
    if (!raw) return results;

    const QStringList words = splitSearchWords(term);
    if (words.isEmpty()) return results;

    QString sql = "SELECT id, path, filename, extension, size, modified_date, is_favorite "
                  "FROM Files WHERE 1=1 ";
    for (int i = 0; i < words.size(); ++i) {
        sql += " AND filename LIKE ? ESCAPE '\\' ";
    }
    sql += " ORDER BY modified_date DESC LIMIT " + QString::number(limit) + ";";

    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) != SQLITE_OK)
        return results;

    int idx = 1;
    for (const auto& w : words) {
        bindText(s, idx++, "%" + likeEscape(w) + "%");
    }

    while (sqlite3_step(s) == SQLITE_ROW) {
        SearchHit h;
        h.fileId      = sqlite3_column_int64(s, 0);
        h.path        = colText(s, 1);
        h.filename    = colText(s, 2);
        h.extension   = colText(s, 3);
        h.size        = sqlite3_column_int64(s, 4);
        h.modifiedDate= QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(s, 5));
        h.isFavorite  = sqlite3_column_int(s, 6) != 0;
        results.append(h);
    }
    sqlite3_finalize(s);
    return results;
}

QStringList SearchEngine::recentSearches(int max) const {
    QSettings s;
    s.beginGroup("recentSearches");
    return s.value("list").toStringList().mid(0, max);
}

void SearchEngine::recordSearch(const QString& q) {
    if (q.trimmed().isEmpty()) return;
    QSettings s;
    s.beginGroup("recentSearches");
    QStringList list = s.value("list").toStringList();
    list.removeAll(q);
    list.prepend(q);
    if (list.size() > 50) list = list.mid(0, 50);
    s.setValue("list", list);
}

} // namespace DocuSearch
