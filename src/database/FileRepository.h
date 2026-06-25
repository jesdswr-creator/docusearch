#pragma once

// ============================================================
// FileRepository.h - Data-access layer for Files/Tags/Notes/FTS
// ============================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <vector>
#include "../core/Types.h"

namespace DocuSearch {

class Database;

class FileRepository : public QObject {
    Q_OBJECT
public:
    explicit FileRepository(Database& db, QObject* parent = nullptr);

    // ---- Files ---------------------------------------------------------

    // Insert or update by path. Returns file id (>0) on success, 0 on failure.
    // Sets indexing_status to 'pending' on first insert.
    qint64 upsertFile(const FileRecord& r);

    // Update content/OCR text + status for a file.
    bool updateContent(qint64 fileId, const QString& text,
                       const QString& source,  // "native" | "ocr" | "both"
                       const QString& indexingStatus,
                       const QString& ocrStatus = QString());

    // Update status flags only.
    bool updateStatus(qint64 fileId, const QString& indexingStatus,
                      const QString& ocrStatus = QString());

    // Mark a file as deleted (remove all rows). Returns true if rows were deleted.
    bool deleteFile(qint64 fileId);
    bool deleteByPath(const QString& path);

    // Lookup by path. Returns false if not found.
    bool getByPath(const QString& path, FileRecord& out);
    bool getById(qint64 id, FileRecord& out);

    // Get next file to index (lowest priority number, oldest first).
    // Returns 0 if queue is empty.
    qint64 nextPendingFile(QString& outPath, QString& outExt);

    // Total file count.
    qint64 totalFiles() const;

    // Count files by indexing status.
    qint64 countByStatus(const QString& status) const;

    // Set/get favorite flag.
    bool setFavorite(qint64 fileId, bool favorite);
    bool incrementOpenCount(qint64 fileId);

    // ---- Tags ----------------------------------------------------------
    bool addTag(qint64 fileId, const QString& tag);
    bool removeTag(qint64 fileId, const QString& tag);
    QStringList getTags(qint64 fileId) const;
    QList<QPair<qint64, QStringList>> getAllTags() const;   // for bulk UI

    // ---- Notes ---------------------------------------------------------
    bool setNote(qint64 fileId, const QString& note);
    QString getNote(qint64 fileId) const;

    // ---- Saved searches ------------------------------------------------
    qint64 saveSearch(const QString& name, const QString& query);
    bool deleteSearch(qint64 id);
    QList<QPair<qint64, QString>> savedSearches() const;        // id, name
    QString savedSearchQuery(qint64 id) const;
    bool renameSearch(qint64 id, const QString& newName);

    // ---- Duplicate detection -------------------------------------------
    // Returns groups of file_ids that share the same hash (non-empty).
    QList<QList<qint64>> duplicatesByHash() const;

    // ---- Maintenance ---------------------------------------------------
    bool vacuum();
    bool reindexFts();
    bool optimize();

private:
    Database& db_;
    bool ftsUpsert(qint64 fileId, const QString& filename,
                   const QString& content, const QString& path,
                   const QString& extension);
};

} // namespace DocuSearch
