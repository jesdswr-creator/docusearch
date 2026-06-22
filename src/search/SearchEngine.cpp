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
} // namespace

SearchEngine::SearchEngine(Database& db, FileRepository& repo, QObject* parent)
    : QObject(parent), db_(db), repo_(repo) {}

QList<SearchHit> SearchEngine::search(const QString& rawQuery, int limit) {
    QList<SearchHit> results;
    sqlite3* raw = db_.raw();
    if (!raw) return results;

    const ParsedQuery q = QueryParser::parse(rawQuery);

    // If the FTS query is empty, do a simple filename LIKE search.
    // This is the safe path - no FTS5 MATCH, no crash risk.
    if (q.ftsQuery.isEmpty()) {
        QString sql = "SELECT id, path, filename, extension, size, "
                      "modified_date, is_favorite "
                      "FROM Files WHERE 1=1 ";
        if (!q.typeFilter.isEmpty())   sql += " AND extension = ? ";
        if (!q.folderFilter.isEmpty()) sql += " AND path LIKE ? ESCAPE '\\' ";
        if (q.favoritesOnly)          sql += " AND is_favorite = 1 ";
        if (!q.tagFilter.isEmpty())   sql += " AND id IN (SELECT file_id FROM Tags WHERE tag = ?) ";
        sql += " ORDER BY modified_date DESC LIMIT " + QString::number(limit) + ";";

        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) != SQLITE_OK) {
            return results;
        }
        int idx = 1;
        if (!q.typeFilter.isEmpty()) {
            const QByteArray u = q.typeFilter.toUtf8();
            sqlite3_bind_text(s, idx++, u.constData(), u.size(), SQLITE_TRANSIENT);
        }
        if (!q.folderFilter.isEmpty()) {
            QString like = q.folderFilter;
            like.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
            like = "%" + like + "%";
            const QByteArray u = like.toUtf8();
            sqlite3_bind_text(s, idx++, u.constData(), u.size(), SQLITE_TRANSIENT);
        }
        if (!q.tagFilter.isEmpty()) {
            const QByteArray u = q.tagFilter.toUtf8();
            sqlite3_bind_text(s, idx++, u.constData(), u.size(), SQLITE_TRANSIENT);
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

    // FTS5 content search - wrapped in try/catch to prevent crashes
    // from malformed FTS5 queries (special chars like ( ) * " etc.)
    try {
        QString sql = "SELECT f.id, f.path, f.filename, f.extension, f.size, "
                      "f.modified_date, f.is_favorite, "
                      "snippet(SearchIndex, 1, '<b>', '</b>', '...', 16) AS snip, "
                      "bm25(SearchIndex) AS score "
                      "FROM SearchIndex s JOIN Files f ON f.id = s.file_id "
                      "WHERE SearchIndex MATCH ? ";
        if (!q.typeFilter.isEmpty())   sql += " AND f.extension = ? ";
        if (!q.folderFilter.isEmpty()) sql += " AND f.path LIKE ? ESCAPE '\\' ";
        if (q.favoritesOnly)          sql += " AND f.is_favorite = 1 ";
        if (!q.tagFilter.isEmpty())   sql += " AND f.id IN (SELECT file_id FROM Tags WHERE tag = ?) ";
        sql += " ORDER BY score ASC LIMIT " + QString::number(limit) + ";";

        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) != SQLITE_OK) {
            // FTS5 query parse error - fall back to filename search
            DS_WARN("Search", QString("FTS prepare failed, falling back to filename: %1").arg(sqlite3_errmsg(raw)));
            return searchByFilename(rawQuery, limit);
        }

        int idx = 1;
        const QByteArray fts = q.ftsQuery.toUtf8();
        sqlite3_bind_text(s, idx++, fts.constData(), fts.size(), SQLITE_TRANSIENT);
        if (!q.typeFilter.isEmpty()) {
            const QByteArray u = q.typeFilter.toUtf8();
            sqlite3_bind_text(s, idx++, u.constData(), u.size(), SQLITE_TRANSIENT);
        }
        if (!q.folderFilter.isEmpty()) {
            QString like = q.folderFilter;
            like.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
            like = "%" + like + "%";
            const QByteArray u = like.toUtf8();
            sqlite3_bind_text(s, idx++, u.constData(), u.size(), SQLITE_TRANSIENT);
        }
        if (!q.tagFilter.isEmpty()) {
            const QByteArray u = q.tagFilter.toUtf8();
            sqlite3_bind_text(s, idx++, u.constData(), u.size(), SQLITE_TRANSIENT);
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
            h.snippet     = colText(s, 7);
            h.score       = static_cast<float>(sqlite3_column_double(s, 8));
            results.append(h);
        }
        sqlite3_finalize(s);
    } catch (...) {
        // Any exception in FTS search - return empty results
        DS_WARN("Search", "FTS search threw exception, returning empty results");
    }

    return results;
}

QList<SearchHit> SearchEngine::searchByFilename(const QString& term, int limit) {
    QList<SearchHit> results;
    sqlite3* raw = db_.raw();
    if (!raw || term.trimmed().isEmpty()) return results;

    QString sql = "SELECT id, path, filename, extension, size, modified_date, is_favorite "
                  "FROM Files WHERE filename LIKE ?1 ESCAPE '\\' "
                  "ORDER BY modified_date DESC LIMIT " + QString::number(limit) + ";";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) != SQLITE_OK) return results;

    QString like = term;
    like.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
    bindText(s, 1, "%" + like + "%");

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
