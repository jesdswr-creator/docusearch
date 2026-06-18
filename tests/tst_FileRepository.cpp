// ============================================================
// tst_FileRepository.cpp — Integration tests for FileRepository
// ============================================================
//
// Creates an in-memory SQLite database (with FTS5 enabled), initializes
// the DocuSearch schema, and exercises the full FileRepository API
// (Files, Tags, Notes, SavedSearches, duplicates, FTS reindex).
//
// Each test starts with a fresh in-memory DB so tests are independent.
//
// Uses the Qt Test framework.
// ============================================================

#include "../src/database/Database.h"
#include "../src/database/Schema.h"
#include "../src/database/FileRepository.h"
#include "../src/core/Types.h"
#include "../src/core/Constants.h"

#include <QtTest/QtTest>
#include <QDateTime>
#include <QFileInfo>
#include <QElapsedTimer>
#include <sqlite3.h>
#include <memory>

using DocuSearch::Database;
using DocuSearch::Schema;
using DocuSearch::FileRepository;
using DocuSearch::FileRecord;
namespace IndexingStatus = DocuSearch::Constants::IndexingStatus;
namespace OcrStatus      = DocuSearch::Constants::OcrStatus;

class TestFileRepository : public QObject {
    Q_OBJECT

private:
    // Helper: build a fresh in-memory DB + schema + repo.
    // Returns true on success.
    bool setup(std::unique_ptr<Database>& db,
               std::unique_ptr<FileRepository>& repo) {
        db = std::make_unique<Database>();
        QString err;
        if (!db->open(":memory:", &err)) {
            qWarning("DB open failed: %s", qPrintable(err));
            return false;
        }
        if (!Schema::initialize(*db)) {
            qWarning("Schema init failed");
            return false;
        }
        repo = std::make_unique<FileRepository>(*db);
        return true;
    }

    // Helper: build a sample FileRecord
    FileRecord makeRecord(const QString& path, const QString& content = "") {
        FileRecord r;
        r.path          = path;
        // Extract filename robustly across both Windows and POSIX paths:
        // take everything after the last '\\' or '/'.
        int lastSep = -1;
        for (int i = path.size() - 1; i >= 0; --i) {
            const QChar c = path.at(i);
            if (c == QLatin1Char('\\') || c == QLatin1Char('/')) {
                lastSep = i;
                break;
            }
        }
        r.filename      = (lastSep >= 0) ? path.mid(lastSep + 1) : path;
        // Strip the extension for the extension field
        int dot = r.filename.lastIndexOf('.');
        r.extension     = (dot >= 0) ? r.filename.mid(dot + 1).toLower() : "";
        r.size          = content.size();
        r.createdDate   = QDateTime::currentDateTime();
        r.modifiedDate  = QDateTime::currentDateTime();
        r.hash          = "abc123";
        r.indexingStatus= IndexingStatus::kPending;
        r.ocrStatus     = OcrStatus::kPending;
        return r;
    }

private slots:

    // ---- upsert + lookup --------------------------------------------------

    void upsertFile_inserts() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\report.pdf");
        const qint64 id = repo->upsertFile(r);
        QVERIFY(id > 0);
        QCOMPARE(repo->totalFiles(), qint64(1));
    }
    void upsertFile_idempotentOnSamePath() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\report.pdf");
        const qint64 id1 = repo->upsertFile(r);
        const qint64 id2 = repo->upsertFile(r);
        // Same path -> same id (no duplicate row)
        QCOMPARE(id1, id2);
        QCOMPARE(repo->totalFiles(), qint64(1));
    }
    void upsertFile_updatesOnNewerModification() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r1 = makeRecord("D:\\Docs\\report.pdf");
        r1.size = 100;
        repo->upsertFile(r1);

        FileRecord r2 = makeRecord("D:\\Docs\\report.pdf");
        r2.size = 200;
        r2.modifiedDate = r1.modifiedDate.addSecs(60);   // newer
        repo->upsertFile(r2);

        FileRecord out;
        QVERIFY(repo->getByPath("D:\\Docs\\report.pdf", out));
        QCOMPARE(out.size, qint64(200));
    }
    void getByPath_returnsFalseIfMissing() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord out;
        QVERIFY(!repo->getByPath("C:\\nonexistent.txt", out));
    }
    void getById_roundtrip() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\file.docx");
        const qint64 id = repo->upsertFile(r);
        QVERIFY(id > 0);

        FileRecord out;
        QVERIFY(repo->getById(id, out));
        QCOMPARE(out.path, QString("D:\\Docs\\file.docx"));
        QCOMPARE(out.filename, QString("file.docx"));
        QCOMPARE(out.extension, QString("docx"));
    }

    // ---- content + FTS ----------------------------------------------------

    void updateContent_insertsDocumentText() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\notes.txt");
        const qint64 id = repo->upsertFile(r);

        QVERIFY(repo->updateContent(id, "NOC examination executive lounge",
                                     "native", IndexingStatus::kContentDone));
        QCOMPARE(repo->countByStatus(IndexingStatus::kContentDone), qint64(1));
    }
    void updateContent_replacesExisting() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\notes.txt");
        const qint64 id = repo->upsertFile(r);

        repo->updateContent(id, "old text",   "native", IndexingStatus::kContentDone);
        repo->updateContent(id, "new text",   "native", IndexingStatus::kContentDone);

        // Verify only ONE DocumentText row exists for this file
        sqlite3* raw = db->raw();
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM DocumentText WHERE file_id = ?1;",
                           -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, id);
        qint64 n = 0;
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
        QCOMPARE(n, qint64(1));
    }

    // ---- status updates ---------------------------------------------------

    void updateStatus_setsFlags() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\file.pdf");
        const qint64 id = repo->upsertFile(r);

        QVERIFY(repo->updateStatus(id, IndexingStatus::kOcrDone, OcrStatus::kDone));
        FileRecord out;
        QVERIFY(repo->getById(id, out));
        QCOMPARE(out.indexingStatus, QString(IndexingStatus::kOcrDone));
        QCOMPARE(out.ocrStatus,      QString(OcrStatus::kDone));
    }

    // ---- delete -----------------------------------------------------------

    void deleteFile_removesRow() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\gone.pdf");
        const qint64 id = repo->upsertFile(r);
        QCOMPARE(repo->totalFiles(), qint64(1));

        QVERIFY(repo->deleteFile(id));
        QCOMPARE(repo->totalFiles(), qint64(0));
    }
    void deleteFile_cascadesToTags() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\tagged.pdf");
        const qint64 id = repo->upsertFile(r);
        repo->addTag(id, "Urgent");
        QCOMPARE(repo->getTags(id).size(), 1);

        repo->deleteFile(id);

        sqlite3* raw = db->raw();
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM Tags WHERE file_id = ?1;",
                           -1, &s, nullptr);
        sqlite3_bind_int64(s, 1, id);
        qint64 n = 0;
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
        QCOMPARE(n, qint64(0));   // cascade delete worked
    }
    void deleteByPath_works() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\bypath.pdf");
        repo->upsertFile(r);

        QVERIFY(repo->deleteByPath("D:\\Docs\\bypath.pdf"));
        QCOMPARE(repo->totalFiles(), qint64(0));
    }

    // ---- nextPendingFile --------------------------------------------------

    void nextPendingFile_returnsPending() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord a = makeRecord("D:\\Docs\\a.pdf");
        FileRecord b = makeRecord("D:\\Docs\\b.pdf");
        const qint64 idA = repo->upsertFile(a);
        const qint64 idB = repo->upsertFile(b);

        QString path, ext;
        const qint64 next = repo->nextPendingFile(path, ext);
        QVERIFY(next == idA || next == idB);
        QVERIFY(!path.isEmpty());
        QVERIFY(!ext.isEmpty());
    }
    void nextPendingFile_skipsDone() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord a = makeRecord("D:\\Docs\\done.pdf");
        FileRecord b = makeRecord("D:\\Docs\\pending.pdf");
        const qint64 idA = repo->upsertFile(a);
        const qint64 idB = repo->upsertFile(b);

        // Mark A as fully indexed
        repo->updateStatus(idA, IndexingStatus::kContentDone);

        QString path, ext;
        const qint64 next = repo->nextPendingFile(path, ext);
        QCOMPARE(next, idB);
    }
    void nextPendingFile_emptyQueue() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));
        // No files inserted
        QString path, ext;
        QCOMPARE(repo->nextPendingFile(path, ext), qint64(0));
    }

    // ---- favorites + open count ------------------------------------------

    void setFavorite_togglesFlag() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\fav.pdf");
        const qint64 id = repo->upsertFile(r);

        QVERIFY(repo->setFavorite(id, true));
        FileRecord out;
        QVERIFY(repo->getById(id, out));
        QVERIFY(out.isFavorite);

        QVERIFY(repo->setFavorite(id, false));
        QVERIFY(repo->getById(id, out));
        QVERIFY(!out.isFavorite);
    }
    void incrementOpenCount_increments() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord r = makeRecord("D:\\Docs\\opened.pdf");
        const qint64 id = repo->upsertFile(r);

        repo->incrementOpenCount(id);
        repo->incrementOpenCount(id);
        repo->incrementOpenCount(id);

        FileRecord out;
        QVERIFY(repo->getById(id, out));
        QCOMPARE(out.openCount, 3);
        QVERIFY(out.lastOpened.isValid());
    }

    // ---- tags -------------------------------------------------------------

    void addTag_inserts() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id = repo->upsertFile(makeRecord("D:\\Docs\\t.pdf"));
        QVERIFY(repo->addTag(id, "Urgent"));
        QVERIFY(repo->addTag(id, "VIP"));
        const QStringList tags = repo->getTags(id);
        QCOMPARE(tags.size(), 2);
        QVERIFY(tags.contains("Urgent"));
        QVERIFY(tags.contains("VIP"));
    }
    void addTag_idempotent() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id = repo->upsertFile(makeRecord("D:\\Docs\\t.pdf"));
        repo->addTag(id, "Urgent");
        repo->addTag(id, "Urgent");   // duplicate
        repo->addTag(id, "Urgent");   // duplicate
        QCOMPARE(repo->getTags(id).size(), 1);
    }
    void removeTag_removesSingleTag() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id = repo->upsertFile(makeRecord("D:\\Docs\\t.pdf"));
        repo->addTag(id, "Urgent");
        repo->addTag(id, "VIP");
        repo->addTag(id, "Pending");

        QVERIFY(repo->removeTag(id, "VIP"));
        const QStringList tags = repo->getTags(id);
        QCOMPARE(tags.size(), 2);
        QVERIFY(tags.contains("Urgent"));
        QVERIFY(tags.contains("Pending"));
        QVERIFY(!tags.contains("VIP"));
    }
    void getAllTags_groupsByFile() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id1 = repo->upsertFile(makeRecord("D:\\Docs\\a.pdf"));
        const qint64 id2 = repo->upsertFile(makeRecord("D:\\Docs\\b.pdf"));
        repo->addTag(id1, "Urgent");
        repo->addTag(id1, "VIP");
        repo->addTag(id2, "Pending");

        const auto all = repo->getAllTags();
        QCOMPARE(all.size(), 2);   // two files have tags
    }

    // ---- notes ------------------------------------------------------------

    void setNote_insertsAndUpdates() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id = repo->upsertFile(makeRecord("D:\\Docs\\n.pdf"));
        QVERIFY(repo->setNote(id, "Reply sent on 12.06.2026"));
        QCOMPARE(repo->getNote(id), QString("Reply sent on 12.06.2026"));

        QVERIFY(repo->setNote(id, "Updated note"));
        QCOMPARE(repo->getNote(id), QString("Updated note"));
    }
    void getNote_returnsEmptyIfMissing() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id = repo->upsertFile(makeRecord("D:\\Docs\\n.pdf"));
        QVERIFY(repo->getNote(id).isEmpty());
    }

    // ---- saved searches ---------------------------------------------------

    void saveSearch_inserts() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id = repo->saveSearch("My NOCs", "type:pdf NOC");
        QVERIFY(id > 0);
        const auto all = repo->savedSearches();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].second, QString("My NOCs"));
        QCOMPARE(repo->savedSearchQuery(id), QString("type:pdf NOC"));
    }
    void saveSearch_upsertsByName() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id1 = repo->saveSearch("Railway Letters", "type:pdf railway");
        const qint64 id2 = repo->saveSearch("Railway Letters", "type:pdf railway updated");
        QCOMPARE(id1, id2);   // same name -> same id, updated query
        QCOMPARE(repo->savedSearches().size(), 1);
        QCOMPARE(repo->savedSearchQuery(id1), QString("type:pdf railway updated"));
    }
    void deleteSearch_removesRow() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id = repo->saveSearch("Temp", "foo");
        QVERIFY(repo->deleteSearch(id));
        QVERIFY(repo->savedSearches().isEmpty());
    }
    void renameSearch_changesName() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        const qint64 id = repo->saveSearch("Old Name", "foo");
        QVERIFY(repo->renameSearch(id, "New Name"));
        const auto all = repo->savedSearches();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].second, QString("New Name"));
    }

    // ---- duplicate detection ----------------------------------------------

    void duplicatesByHash_findsDupes() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord a = makeRecord("D:\\Docs\\a.pdf"); a.hash = "hashXYZ";
        FileRecord b = makeRecord("D:\\Docs\\b.pdf"); b.hash = "hashXYZ";   // same hash
        FileRecord c = makeRecord("D:\\Docs\\c.pdf"); c.hash = "hash123";   // different
        repo->upsertFile(a);
        repo->upsertFile(b);
        repo->upsertFile(c);

        const auto groups = repo->duplicatesByHash();
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups[0].size(), 2);   // a + b share a hash
    }
    void duplicatesByHash_ignoresEmptyHash() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        FileRecord a = makeRecord("D:\\Docs\\a.pdf"); a.hash = "";
        FileRecord b = makeRecord("D:\\Docs\\b.pdf"); b.hash = "";
        repo->upsertFile(a);
        repo->upsertFile(b);

        // Two files with empty hash should NOT be flagged as duplicates
        QVERIFY(repo->duplicatesByHash().isEmpty());
    }

    // ---- maintenance ------------------------------------------------------

    void reindexFts_rebuildsSearchIndex() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        // Insert a couple of files with content
        const qint64 id1 = repo->upsertFile(makeRecord("D:\\Docs\\a.pdf"));
        repo->updateContent(id1, "alpha beta", "native", IndexingStatus::kContentDone);
        const qint64 id2 = repo->upsertFile(makeRecord("D:\\Docs\\b.pdf"));
        repo->updateContent(id2, "gamma delta", "native", IndexingStatus::kContentDone);

        QVERIFY(repo->reindexFts());

        // Verify the FTS index has 2 rows
        sqlite3* raw = db->raw();
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM SearchIndex;", -1, &s, nullptr);
        qint64 n = 0;
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
        QCOMPARE(n, qint64(2));
    }
    void vacuum_runsWithoutError() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));
        QVERIFY(repo->vacuum());
    }
    void optimize_runsWithoutError() {
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));
        QVERIFY(repo->optimize());
    }

    // ---- bulk insert performance smoke ----------------------------------

    void bulkInsert_smokeTest() {
        // Sanity check: we should be able to insert 1k files in well under a
        // second on any reasonable machine. This catches accidental O(N²)
        // regressions in the upsert path.
        std::unique_ptr<Database> db;
        std::unique_ptr<FileRepository> repo;
        QVERIFY(setup(db, repo));

        QElapsedTimer t; t.start();
        const int N = 1000;
        for (int i = 0; i < N; ++i) {
            FileRecord r = makeRecord(QString("D:\\Bulk\\file_%1.pdf").arg(i));
            repo->upsertFile(r);
        }
        const qint64 ms = t.elapsed();
        QCOMPARE(repo->totalFiles(), qint64(N));
        QVERIFY2(ms < 5000,
                 QString("Bulk insert of %1 files took %2 ms").arg(N).arg(ms).toUtf8().constData());
    }
};

QTEST_GUILESS_MAIN(TestFileRepository)
#include "tst_FileRepository.moc"
