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

    // Build a SQL query that joins SearchIndex (FTS5) with Files, then filters.
    // We use anonymous '?' placeholders which auto-increment from 1 in the
    // order they appear in the SQL string. Binding must happen in the same order.
    QString sql;
    bool usedFts = false;

    if (!q.ftsQuery.isEmpty()) {
        // Use FTS5 MATCH
        sql = "SELECT f.id, f.path, f.filename, f.extension, f.size, "
              "f.modified_date, f.is_favorite, "
              "snippet(SearchIndex, 1, '<b>', '</b>', '…', 16) AS snip, "
              "bm25(SearchIndex) AS score "
              "FROM SearchIndex s JOIN Files f ON f.id = s.file_id "
              "WHERE SearchIndex MATCH ? ";
        usedFts = true;
    } else {
        // Filename-only fast LIKE
        sql = "SELECT f.id, f.path, f.filename, f.extension, f.size, "
              "f.modified_date, f.is_favorite, "
              "'' AS snip, 0.0 AS score "
              "FROM Files f WHERE 1=1 ";
    }

    if (!q.typeFilter.isEmpty()) {
        sql += " AND f.extension = ? ";
    }
    if (!q.folderFilter.isEmpty()) {
        sql += " AND f.path LIKE ? ESCAPE '\\' ";
    }
    if (!q.dateFilter.isEmpty()) {
        // Year filter
        bool ok = false;
        const int year = q.dateFilter.toInt(&ok);
        if (ok && year > 1900) {
            sql += QString(" AND strftime('%Y', f.modified_date, 'unixepoch') = '%1' ").arg(year);
        } else {
            // Try "YYYY-MM" or "YYYY-MM-DD"
            sql += QString(" AND date(f.modified_date, 'unixepoch') LIKE '%1%' ").arg(q.dateFilter);
        }
    }
    if (q.favoritesOnly) {
        sql += " AND f.is_favorite = 1 ";
    }
    if (q.ocrOnly) {
        sql += " AND f.ocr_status = 'done' ";
    }
    if (!q.tagFilter.isEmpty()) {
        // Files that have this tag. Uses the UNIQUE(file_id, tag) index on Tags
        // for an index lookup — fast even with millions of files.
        sql += " AND f.id IN (SELECT file_id FROM Tags WHERE tag = ?) ";
    }
    if (usedFts) {
        sql += " ORDER BY score ASC ";   // bm25: lower is better
    } else {
        sql += " ORDER BY f.modified_date DESC ";
    }
    sql += QString(" LIMIT %1;").arg(limit);

    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) != SQLITE_OK) {
        DS_ERROR("Search", QString("Prepare failed: %1 | SQL: %2").arg(sqlite3_errmsg(raw), sql));
        return results;
    }

    // Bind in the SAME ORDER as the '?' placeholders appear above.
    if (usedFts)                       bindText(s, 1, q.ftsQuery);
    int bindIdx = usedFts ? 2 : 1;
    if (!q.typeFilter.isEmpty())       bindText(s, bindIdx++, q.typeFilter);
    if (!q.folderFilter.isEmpty()) {
        QString like = q.folderFilter;
        like.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        bindText(s, bindIdx++, "%" + like + "%");
    }
    if (!q.tagFilter.isEmpty())        bindText(s, bindIdx++, q.tagFilter);

    QElapsedTimer t; t.start();
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

    if (t.elapsed() > 100) {
        DS_DEBUG("Search", QString("Slow search (%1 ms): %2")
                 .arg(t.elapsed()).arg(rawQuery));
    }

    recordSearch(rawQuery);
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
