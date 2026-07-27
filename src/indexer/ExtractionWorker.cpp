// ============================================================
// ExtractionWorker.cpp - Off-main-thread extraction implementation
// ============================================================

#include "ExtractionWorker.h"
#include "../documents/DocumentExtractorRegistry.h"
#include "../core/Logger.h"
#include "../core/Constants.h"
#include "../core/SehTranslator.h"

#include <QFileInfo>
#include <QDateTime>
#include <QThread>
#include <sqlite3.h>

namespace DocuSearch {

ExtractionWorker::ExtractionWorker(QObject* parent)
    : QObject(parent) {}

ExtractionWorker::~ExtractionWorker() = default;

void ExtractionWorker::setTodo(const QList<ExtractionTodoItem>& todo,
                                const QString& dbPath,
                                bool   generateEmbeddings) {
    todo_               = todo;
    dbPath_             = dbPath;
    generateEmbeddings_ = generateEmbeddings;
}

void ExtractionWorker::cancelExtraction() {
    cancelFlag_.storeRelaxed(1);
}

// ── Main loop ────────────────────────────────────────────────
void ExtractionWorker::run() {
    // CRITICAL: install the SEH translator ON THIS THREAD.
    // _set_se_translator() is per-thread, and main() only installs it on the
    // main thread. Without this, an access violation inside PDF/DOCX/XLSX
    // parsers would crash the process instead of being caught by catch(...).
    installSehTranslator();

    const int total = todo_.size();
    if (total == 0) {
        emit finished(0, 0, 0);
        return;
    }

    // Open a SEPARATE SQLite connection on this worker thread.
    // We use a write-friendly busy_timeout since the main thread may
    // also be doing reads. WAL mode (set in Database.cpp) makes
    // concurrent reader + one writer safe.
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(dbPath_.toUtf8().constData(),
                             &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX,
                             nullptr);
    if (rc != SQLITE_OK || !db) {
        DS_ERROR("Extract", "Worker: failed to open DB connection — aborting batch.");
        emit finished(0, total, total);
        return;
    }

    sqlite3_busy_timeout(db, 30000);  // 30s on contention
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;",   nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    int succeeded = 0;
    int failed    = 0;

    for (int i = 0; i < total; ++i) {
        if (cancelFlag_.loadRelaxed()) break;

        const auto& item = todo_[i];
        QFileInfo fi(item.path);
        ExtractionProgress result;
        result.fileId   = item.fileId;
        result.path     = item.path;
        result.filename = fi.fileName();

        try {
            // ── Missing file ────────────────────────────────────
            if (!QFileInfo::exists(item.path)) {
                result.missingFile = true;
                sqlite3_exec(db,
                    QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
                ++failed;
                emit fileExtracted(result);
                emit progress(i + 1, total);
                continue;
            }

            // ── Too large ───────────────────────────────────────
            if (fi.size() > Constants::kMaxFilesizeToExtract) {
                result.skippedTooLarge = true;
                sqlite3_exec(db,
                    QString("UPDATE Files SET indexing_status='skipped' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
                ++failed;
                emit fileExtracted(result);
                emit progress(i + 1, total);
                continue;
            }

            // ── Extract via the registry (PDF/DOCX/XLSX/PPTX/TXT) ─
            auto extracted = DocumentExtractorRegistry::instance()
                                .extractByExtension(item.path, item.ext);
            result.extractedText = extracted.text;
            result.source        = extracted.source.isEmpty() ? "native" : extracted.source;

            if (extracted.needsOcr && result.extractedText.isEmpty()) {
                // Mark for OCR — main thread can pick this up later.
                result.needsOcr = true;
                sqlite3_exec(db,
                    QString("UPDATE Files SET indexing_status='needs_ocr' WHERE id=%1;")
                        .arg(item.fileId).toUtf8().constData(),
                    nullptr, nullptr, nullptr);
                ++succeeded;  // count as processed (just needs OCR later)
                result.ok = true;
                emit fileExtracted(result);
                emit progress(i + 1, total);
                continue;
            }

            // ── Truncate to safety cap ──────────────────────────
            if (result.extractedText.size() > Constants::kMaxExtractTextChars) {
                result.extractedText =
                    result.extractedText.left(Constants::kMaxExtractTextChars) +
                    "\n\n[... text truncated for memory ...]";
            }

            // ── Write to DocumentText + SearchIndex ─────────────
            const QByteArray textBytes = result.extractedText.toUtf8();
            const QByteArray srcBytes  = result.source.toUtf8();
            const qint64 charCount     = result.extractedText.size();
            const qint64 now           = QDateTime::currentSecsSinceEpoch();

            // DocumentText (UPSERT).
            sqlite3_stmt* upd = nullptr;
            sqlite3_prepare_v2(db,
                "INSERT INTO DocumentText (file_id, extracted_text, text_source, char_count, updated_at) "
                "VALUES (?1, ?2, ?3, ?4, ?5) "
                "ON CONFLICT(file_id) DO UPDATE SET "
                "  extracted_text=excluded.extracted_text, "
                "  text_source=excluded.text_source, "
                "  char_count=excluded.char_count, "
                "  updated_at=excluded.updated_at;",
                -1, &upd, nullptr);
            if (upd) {
                sqlite3_bind_int64(upd, 1, item.fileId);
                sqlite3_bind_text(upd, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(upd, 3, srcBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(upd, 4, charCount);
                sqlite3_bind_int64(upd, 5, now);
                sqlite3_step(upd);
                sqlite3_finalize(upd);
            }

            // Files.status update.
            sqlite3_exec(db,
                QString("UPDATE Files SET indexing_status='content_done', ocr_status='not_needed' WHERE id=%1;")
                    .arg(item.fileId).toUtf8().constData(),
                nullptr, nullptr, nullptr);

            // SearchIndex: delete + re-insert.
            sqlite3_stmt* del = nullptr;
            sqlite3_prepare_v2(db, "DELETE FROM SearchIndex WHERE file_id=?1;",
                               -1, &del, nullptr);
            if (del) {
                sqlite3_bind_int64(del, 1, item.fileId);
                sqlite3_step(del);
                sqlite3_finalize(del);
            }

            const QByteArray fn  = fi.fileName().toUtf8();
            const QByteArray pth = item.path.toUtf8();
            const QByteArray ext = item.ext.toUtf8();
            sqlite3_stmt* ins = nullptr;
            sqlite3_prepare_v2(db,
                "INSERT INTO SearchIndex (filename, content, path, extension, file_id) "
                "VALUES (?1, ?2, ?3, ?4, ?5);",
                -1, &ins, nullptr);
            if (ins) {
                sqlite3_bind_text(ins, 1, fn.constData(),  -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 2, textBytes.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 3, pth.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 4, ext.constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins, 5, item.fileId);
                sqlite3_step(ins);
                sqlite3_finalize(ins);
            }

            result.ok = true;
            ++succeeded;
        } catch (const std::exception& e) {
            DS_WARN("Extract", QString("Failed: %1 — %2").arg(item.path).arg(e.what()));
            result.ok           = false;
            result.errorMessage = e.what();
            sqlite3_exec(db,
                QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                    .arg(item.fileId).toUtf8().constData(),
                nullptr, nullptr, nullptr);
            ++failed;
        } catch (...) {
            result.ok           = false;
            result.errorMessage = "unknown exception";
            sqlite3_exec(db,
                QString("UPDATE Files SET indexing_status='failed' WHERE id=%1;")
                    .arg(item.fileId).toUtf8().constData(),
                nullptr, nullptr, nullptr);
            ++failed;
        }

        emit fileExtracted(result);
        emit progress(i + 1, total);
    }

    sqlite3_close(db);
    emit finished(succeeded, failed, total);
}

} // namespace DocuSearch
