// ============================================================
// FileRepository.cpp
// ============================================================

#include "FileRepository.h"
#include "Database.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"
#include "../core/Constants.h"

#include <sqlite3.h>
#include <QDateTime>
#include <QVariant>
#include <algorithm>

namespace DocuSearch {

namespace {
    // Bind a QString (UTF-8) by index (1-based).
    void bindText(sqlite3_stmt* s, int idx, const QString& v) {
        const QByteArray utf8 = v.toUtf8();
        sqlite3_bind_text(s, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
    }
    QString colText(sqlite3_stmt* s, int idx) {
        const unsigned char* t = sqlite3_column_text(s, idx);
        return t ? QString::fromUtf8(reinterpret_cast<const char*>(t)) : QString();
    }
}

FileRepository::FileRepository(Database& db, QObject* parent)
    : QObject(parent), db_(db) {}

qint64 FileRepository::upsertFile(const FileRecord& r) {
    sqlite3* raw = db_.raw();
    if (!raw) return 0;
    constexpr const char* kSql =
        "INSERT INTO Files (path, filename, extension, size, created_date, modified_date, "
        "                    hash, indexing_status, ocr_status, indexed_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10) "
        "ON CONFLICT(path) DO UPDATE SET "
        "  filename=excluded.filename, "
        "  extension=excluded.extension, "
        "  size=excluded.size, "
        "  created_date=excluded.created_date, "
        "  modified_date=excluded.modified_date, "
        "  hash=excluded.hash "
        "WHERE modified_date < excluded.modified_date OR size != excluded.size;";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw, kSql, -1, &s, nullptr) != SQLITE_OK) {
        DS_ERROR("Repo", "upsertFile prepare failed");
        return 0;
    }
    bindText(s, 1, r.path);
    bindText(s, 2, r.filename);
    bindText(s, 3, r.extension);
    sqlite3_bind_int64(s, 4, r.size);
    sqlite3_bind_int64(s, 5, r.createdDate.toSecsSinceEpoch());
    sqlite3_bind_int64(s, 6, r.modifiedDate.toSecsSinceEpoch());
    bindText(s, 7, r.hash);
    bindText(s, 8, r.indexingStatus.isEmpty() ? Constants::IndexingStatus::kPending : r.indexingStatus);
    bindText(s, 9, r.ocrStatus.isEmpty() ? Constants::OcrStatus::kPending : r.ocrStatus);
    sqlite3_bind_int64(s, 10, QDateTime::currentSecsSinceEpoch());

    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) {
        DS_ERROR("Repo", QString("upsertFile step failed: %1").arg(sqlite3_errmsg(raw)));
        return 0;
    }
    // For ON CONFLICT DO UPDATE, sqlite3_last_insert_rowid may return existing id.
    // Resolve explicitly by path.
    sqlite3_stmt* s2 = nullptr;
    qint64 id = 0;
    if (sqlite3_prepare_v2(raw, "SELECT id FROM Files WHERE path = ?1;", -1, &s2, nullptr) == SQLITE_OK) {
        bindText(s2, 1, r.path);
        if (sqlite3_step(s2) == SQLITE_ROW) id = sqlite3_column_int64(s2, 0);
        sqlite3_finalize(s2);
    }
    return id;
}

bool FileRepository::updateContent(qint64 fileId, const QString& text,
                                   const QString& source,
                                   const QString& indexingStatus,
                                   const QString& ocrStatus) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;

    db_.begin();
    // Upsert document text
    sqlite3_stmt* s = nullptr;
    constexpr const char* kSql =
        "INSERT INTO DocumentText (file_id, extracted_text, text_source, char_count, updated_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5) "
        "ON CONFLICT(file_id) DO UPDATE SET "
        "  extracted_text=excluded.extracted_text, "
        "  text_source=excluded.text_source, "
        "  char_count=excluded.char_count, "
        "  updated_at=excluded.updated_at;";
    if (sqlite3_prepare_v2(raw, kSql, -1, &s, nullptr) != SQLITE_OK) {
        db_.rollback(); return false;
    }
    sqlite3_bind_int64(s, 1, fileId);
    bindText(s, 2, text);
    bindText(s, 3, source);
    sqlite3_bind_int64(s, 4, static_cast<qint64>(text.size()));
    sqlite3_bind_int64(s, 5, QDateTime::currentSecsSinceEpoch());
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) { db_.rollback(); return false; }

    // Update status flags
    if (!indexingStatus.isEmpty() || !ocrStatus.isEmpty()) {
        QString sql = "UPDATE Files SET ";
        QStringList sets;
        if (!indexingStatus.isEmpty()) sets << "indexing_status = ?2";
        if (!ocrStatus.isEmpty())      sets << "ocr_status = ?3";
        sql += sets.join(", ") + " WHERE id = ?1;";
        sqlite3_stmt* s2 = nullptr;
        if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s2, nullptr) != SQLITE_OK) {
            db_.rollback(); return false;
        }
        sqlite3_bind_int64(s2, 1, fileId);
        if (!indexingStatus.isEmpty()) bindText(s2, 2, indexingStatus);
        if (!ocrStatus.isEmpty())      bindText(s2, 3, ocrStatus);
        const int rc2 = sqlite3_step(s2);
        sqlite3_finalize(s2);
        if (rc2 != SQLITE_DONE) { db_.rollback(); return false; }
    }

    // Update FTS
    FileRecord r;
    if (getById(fileId, r)) {
        ftsUpsert(fileId, r.filename, text, r.path, r.extension);
    }

    db_.commit();
    return true;
}

bool FileRepository::updateStatus(qint64 fileId, const QString& indexingStatus,
                                  const QString& ocrStatus) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    QString sql = "UPDATE Files SET ";
    QStringList sets;
    if (!indexingStatus.isEmpty()) sets << "indexing_status = ?2";
    if (!ocrStatus.isEmpty())      sets << "ocr_status = ?3";
    sql += sets.join(", ") + " WHERE id = ?1;";
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(raw, sql.toUtf8().constData(), -1, &s, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(s, 1, fileId);
    if (!indexingStatus.isEmpty()) bindText(s, 2, indexingStatus);
    if (!ocrStatus.isEmpty())      bindText(s, 3, ocrStatus);
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

bool FileRepository::deleteFile(qint64 fileId) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    db_.begin();
    // Cascade deletes handle Tags, Notes, DocumentText.
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "DELETE FROM Files WHERE id = ?1;", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fileId);
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);

    sqlite3_stmt* s2 = nullptr;
    sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id = ?1;", -1, &s2, nullptr);
    sqlite3_bind_int64(s2, 1, fileId);
    sqlite3_step(s2);
    sqlite3_finalize(s2);

    db_.commit();
    return rc == SQLITE_DONE;
}

bool FileRepository::deleteByPath(const QString& path) {
    FileRecord r;
    if (!getByPath(path, r)) return false;
    return deleteFile(r.id);
}

bool FileRepository::getByPath(const QString& path, FileRecord& out) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT id, path, filename, extension, size, created_date, "
                            "modified_date, hash, indexing_status, ocr_status, is_favorite, "
                            "open_count, last_opened FROM Files WHERE path = ?1;", -1, &s, nullptr);
    bindText(s, 1, path);
    bool found = false;
    if (sqlite3_step(s) == SQLITE_ROW) {
        out.id             = sqlite3_column_int64(s, 0);
        out.path           = colText(s, 1);
        out.filename       = colText(s, 2);
        out.extension      = colText(s, 3);
        out.size           = sqlite3_column_int64(s, 4);
        out.createdDate    = QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(s, 5));
        out.modifiedDate   = QDateTime::fromSecsSinceEpoch(sqlite3_column_int64(s, 6));
        out.hash           = colText(s, 7);
        out.indexingStatus = colText(s, 8);
        out.ocrStatus      = colText(s, 9);
        out.isFavorite     = sqlite3_column_int(s, 10) != 0;
        out.openCount      = sqlite3_column_int(s, 11);
        const qint64 last  = sqlite3_column_int64(s, 12);
        out.lastOpened     = last ? QDateTime::fromSecsSinceEpoch(last) : QDateTime();
        found = true;
    }
    sqlite3_finalize(s);
    if (found) {
        out.tags = getTags(out.id);
        out.note = getNote(out.id);
    }
    return found;
}

bool FileRepository::getById(qint64 id, FileRecord& out) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT path FROM Files WHERE id = ?1;", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    QString p;
    if (sqlite3_step(s) == SQLITE_ROW) p = colText(s, 0);
    sqlite3_finalize(s);
    if (p.isEmpty()) return false;
    return getByPath(p, out);
}

qint64 FileRepository::nextPendingFile(QString& outPath, QString& outExt) {
    sqlite3* raw = db_.raw();
    if (!raw) return 0;
    // Files with content_done OR ocr_done status are considered "in progress" — skip them.
    // Prefer: pending files, oldest first.
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw,
        "SELECT id, path, extension FROM Files "
        "WHERE indexing_status IN ('pending', 'metadata_only') "
        "ORDER BY modified_date DESC LIMIT 1;", -1, &s, nullptr);
    qint64 id = 0;
    if (sqlite3_step(s) == SQLITE_ROW) {
        id      = sqlite3_column_int64(s, 0);
        outPath = colText(s, 1);
        outExt  = colText(s, 2);
    }
    sqlite3_finalize(s);
    return id;
}

qint64 FileRepository::totalFiles() const {
    sqlite3* raw = db_.raw();
    if (!raw) return 0;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM Files;", -1, &s, nullptr);
    qint64 n = 0;
    if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    return n;
}

qint64 FileRepository::countByStatus(const QString& status) const {
    sqlite3* raw = db_.raw();
    if (!raw) return 0;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM Files WHERE indexing_status = ?1;", -1, &s, nullptr);
    bindText(s, 1, status);
    qint64 n = 0;
    if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    return n;
}

bool FileRepository::setFavorite(qint64 fileId, bool favorite) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "UPDATE Files SET is_favorite = ?2 WHERE id = ?1;", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fileId);
    sqlite3_bind_int(s, 2, favorite ? 1 : 0);
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

bool FileRepository::incrementOpenCount(qint64 fileId) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw,
        "UPDATE Files SET open_count = open_count + 1, last_opened = ?2 WHERE id = ?1;",
        -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fileId);
    sqlite3_bind_int64(s, 2, QDateTime::currentSecsSinceEpoch());
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

// ---- Tags ----------------------------------------------------------
bool FileRepository::addTag(qint64 fileId, const QString& tag) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "INSERT OR IGNORE INTO Tags (file_id, tag) VALUES (?1, ?2);",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fileId);
    bindText(s, 2, tag.trimmed());
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

bool FileRepository::removeTag(qint64 fileId, const QString& tag) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "DELETE FROM Tags WHERE file_id = ?1 AND tag = ?2;",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fileId);
    bindText(s, 2, tag);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

QStringList FileRepository::getTags(qint64 fileId) const {
    sqlite3* raw = db_.raw();
    if (!raw) return {};
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT tag FROM Tags WHERE file_id = ?1 ORDER BY tag;",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fileId);
    QStringList out;
    while (sqlite3_step(s) == SQLITE_ROW) out << colText(s, 0);
    sqlite3_finalize(s);
    return out;
}

QList<QPair<qint64, QStringList>> FileRepository::getAllTags() const {
    sqlite3* raw = db_.raw();
    if (!raw) return {};
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT file_id, tag FROM Tags ORDER BY file_id;",
                       -1, &s, nullptr);
    QList<QPair<qint64, QStringList>> out;
    qint64 curId = 0;
    QStringList curList;
    while (sqlite3_step(s) == SQLITE_ROW) {
        const qint64 fid = sqlite3_column_int64(s, 0);
        const QString t = colText(s, 1);
        if (fid != curId) {
            if (curId != 0) out.append({curId, curList});
            curId = fid;
            curList.clear();
        }
        curList << t;
    }
    if (curId != 0) out.append({curId, curList});
    sqlite3_finalize(s);
    return out;
}

// ---- Notes ---------------------------------------------------------
bool FileRepository::setNote(qint64 fileId, const QString& note) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw,
        "INSERT INTO Notes (file_id, note, updated_at) VALUES (?1, ?2, ?3) "
        "ON CONFLICT(file_id) DO UPDATE SET note=excluded.note, updated_at=excluded.updated_at;",
        -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fileId);
    bindText(s, 2, note);
    sqlite3_bind_int64(s, 3, QDateTime::currentSecsSinceEpoch());
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

QString FileRepository::getNote(qint64 fileId) const {
    sqlite3* raw = db_.raw();
    if (!raw) return {};
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT note FROM Notes WHERE file_id = ?1;", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, fileId);
    QString out;
    if (sqlite3_step(s) == SQLITE_ROW) out = colText(s, 0);
    sqlite3_finalize(s);
    return out;
}

// ---- Saved searches ------------------------------------------------
qint64 FileRepository::saveSearch(const QString& name, const QString& query) {
    sqlite3* raw = db_.raw();
    if (!raw) return 0;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw,
        "INSERT INTO SavedSearches (search_name, search_query, created_at) VALUES (?1, ?2, ?3) "
        "ON CONFLICT(search_name) DO UPDATE SET search_query=excluded.search_query;",
        -1, &s, nullptr);
    bindText(s, 1, name);
    bindText(s, 2, query);
    sqlite3_bind_int64(s, 3, QDateTime::currentSecsSinceEpoch());
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc != SQLITE_DONE) return 0;

    sqlite3_stmt* s2 = nullptr;
    qint64 id = 0;
    sqlite3_prepare_v2(raw, "SELECT id FROM SavedSearches WHERE search_name = ?1;",
                       -1, &s2, nullptr);
    bindText(s2, 1, name);
    if (sqlite3_step(s2) == SQLITE_ROW) id = sqlite3_column_int64(s2, 0);
    sqlite3_finalize(s2);
    return id;
}

bool FileRepository::deleteSearch(qint64 id) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "DELETE FROM SavedSearches WHERE id = ?1;", -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    return true;
}

QList<QPair<qint64, QString>> FileRepository::savedSearches() const {
    sqlite3* raw = db_.raw();
    if (!raw) return {};
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT id, search_name FROM SavedSearches ORDER BY search_name;",
                       -1, &s, nullptr);
    QList<QPair<qint64, QString>> out;
    while (sqlite3_step(s) == SQLITE_ROW) {
        out.append({sqlite3_column_int64(s, 0), colText(s, 1)});
    }
    sqlite3_finalize(s);
    return out;
}

QString FileRepository::savedSearchQuery(qint64 id) const {
    sqlite3* raw = db_.raw();
    if (!raw) return {};
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "SELECT search_query FROM SavedSearches WHERE id = ?1;",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    QString out;
    if (sqlite3_step(s) == SQLITE_ROW) out = colText(s, 0);
    sqlite3_finalize(s);
    return out;
}

bool FileRepository::renameSearch(qint64 id, const QString& newName) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw, "UPDATE SavedSearches SET search_name = ?2 WHERE id = ?1;",
                       -1, &s, nullptr);
    sqlite3_bind_int64(s, 1, id);
    bindText(s, 2, newName);
    const int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    return rc == SQLITE_DONE;
}

// ---- Duplicate detection -------------------------------------------
QList<QList<qint64>> FileRepository::duplicatesByHash() const {
    sqlite3* raw = db_.raw();
    if (!raw) return {};
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw,
        "SELECT hash, GROUP_CONCAT(id) FROM Files "
        "WHERE hash != '' GROUP BY hash HAVING COUNT(*) > 1;",
        -1, &s, nullptr);
    QList<QList<qint64>> out;
    while (sqlite3_step(s) == SQLITE_ROW) {
        const QString ids = colText(s, 1);
        QList<qint64> group;
        for (const auto& tok : ids.split(',', Qt::SkipEmptyParts))
            group.append(tok.toLongLong());
        out.append(group);
    }
    sqlite3_finalize(s);
    return out;
}

// ---- Maintenance ---------------------------------------------------
bool FileRepository::vacuum() {
    return db_.exec("VACUUM;");
}

bool FileRepository::reindexFts() {
    // Drop & rebuild the FTS index from Files + DocumentText.
    db_.exec("DELETE FROM SearchIndex;");
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(raw,
        "SELECT f.id, f.filename, COALESCE(d.extracted_text, ''), f.path, f.extension "
        "FROM Files f LEFT JOIN DocumentText d ON d.file_id = f.id;",
        -1, &s, nullptr);
    db_.begin();
    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(raw,
        "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
        "VALUES (?1, ?2, ?3, ?4, ?5);", -1, &ins, nullptr);
    int n = 0;
    while (sqlite3_step(s) == SQLITE_ROW) {
        bindText(ins, 1, colText(s, 1));
        bindText(ins, 2, colText(s, 2));
        bindText(ins, 3, colText(s, 3));
        bindText(ins, 4, colText(s, 4));
        sqlite3_bind_int64(ins, 5, sqlite3_column_int64(s, 0));
        if (sqlite3_step(ins) != SQLITE_DONE) {
            DS_WARN("Repo", "FTS reindex insert failed");
        }
        sqlite3_reset(ins);
        sqlite3_clear_bindings(ins);
        ++n;
        if (n % 1000 == 0) db_.commit(), db_.begin();
    }
    sqlite3_finalize(s);
    sqlite3_finalize(ins);
    db_.commit();
    DS_INFO("Repo", QString("FTS reindex complete (%1 rows)").arg(n));
    return true;
}

bool FileRepository::optimize() {
    db_.exec("INSERT INTO SearchIndex(SearchIndex) VALUES('optimize');");
    db_.exec("ANALYZE;");
    return true;
}

bool FileRepository::ftsUpsert(qint64 fileId, const QString& filename,
                               const QString& content, const QString& path,
                               const QString& extension) {
    sqlite3* raw = db_.raw();
    if (!raw) return false;
    // Delete existing FTS row, then insert.
    sqlite3_stmt* del = nullptr;
    sqlite3_prepare_v2(raw, "DELETE FROM SearchIndex WHERE file_id = ?1;",
                       -1, &del, nullptr);
    sqlite3_bind_int64(del, 1, fileId);
    sqlite3_step(del);
    sqlite3_finalize(del);

    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(raw,
        "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
        "VALUES (?1, ?2, ?3, ?4, ?5);", -1, &ins, nullptr);
    bindText(ins, 1, filename);
    bindText(ins, 2, content);
    bindText(ins, 3, path);
    bindText(ins, 4, extension);
    sqlite3_bind_int64(ins, 5, fileId);
    const int rc = sqlite3_step(ins);
    sqlite3_finalize(ins);
    return rc == SQLITE_DONE;
}

} // namespace DocuSearch
