#pragma once

// ============================================================
// Types.h - Common value types used across modules
// ============================================================

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QHash>
#include <vector>
#include <memory>
#include <atomic>

namespace DocuSearch {

// The single in-memory representation of an indexed file
struct FileRecord {
    qint64      id            = 0;
    QString     path;            // absolute, native separators
    QString     filename;
    QString     extension;       // lowercase, no dot
    qint64      size            = 0;
    QDateTime   createdDate;
    QDateTime   modifiedDate;
    QString     hash;
    QString     indexingStatus;
    QString     ocrStatus;
    QStringList tags;
    QString     note;
    bool        isFavorite      = false;
    int         openCount       = 0;
    QDateTime   lastOpened;

    bool isValid() const { return !path.isEmpty(); }
};

// Search hit
struct SearchHit {
    qint64    fileId        = 0;
    QString   path;
    QString   filename;
    QString   extension;
    qint64    size          = 0;
    QDateTime modifiedDate;
    QString   snippet;          // best snippet around match
    float     score         = 0.0f;
    bool      isFavorite    = false;
};

// Search query AST
enum class QueryNodeType {
    Term, Phrase, Field, And, Or, Not, Wildcard, Empty
};

struct QueryNode;
using QueryNodePtr = std::shared_ptr<QueryNode>;

struct QueryNode {
    QueryNodeType  type   = QueryNodeType::Empty;
    QString        text;       // for Term/Phrase
    QString        field;      // for Field (e.g., "type", "date", "folder")
    QueryNodePtr   left;
    QueryNodePtr   right;

    static QueryNodePtr makeTerm(const QString& s) {
        auto n = std::make_shared<QueryNode>();
        n->type = QueryNodeType::Term;
        n->text = s;
        return n;
    }
    static QueryNodePtr makePhrase(const QString& s) {
        auto n = std::make_shared<QueryNode>();
        n->type = QueryNodeType::Phrase;
        n->text = s;
        return n;
    }
    static QueryNodePtr makeField(const QString& f, const QString& v) {
        auto n = std::make_shared<QueryNode>();
        n->type = QueryNodeType::Field;
        n->field = f;
        n->text  = v;
        return n;
    }
    static QueryNodePtr makeBool(QueryNodeType t, QueryNodePtr l, QueryNodePtr r = nullptr) {
        auto n = std::make_shared<QueryNode>();
        n->type  = t;
        n->left  = l;
        n->right = r;
        return n;
    }
};

// Indexing progress counters
struct IndexingProgress {
    std::atomic<qint64> filesScanned    {0};
    std::atomic<qint64> documentsIndexed{0};
    std::atomic<qint64> ocrCompleted    {0};
    std::atomic<qint64> queueRemaining  {0};
    std::atomic<qint64> errorsCount     {0};
    std::atomic<bool>   paused          {false};
    std::atomic<int>    cpuUsagePct     {0};

    IndexingProgress() = default;
    // Non-copyable due to atomics; explicit copy helper
    IndexingProgress(const IndexingProgress&)            = delete;
    IndexingProgress& operator=(const IndexingProgress&) = delete;
};

// Indexing priority bands
enum class IndexPriority {
    P1_Recent30Days = 1,   // modified < 30 days
    P2_LastYear     = 2,   // 30 .. 365 days
    P3_Older        = 3,   // > 365 days
    P4_Archives     = 4    // zip/rar/7z (extract-and-scan, optional)
};

// Settings payload
struct AppSettings {
    QStringList indexedDrives;          // e.g., {"D:\\"}
    QStringList excludedFolders;        // e.g., {"D:\\Movies"}
    QStringList excludedExtensions;     // user-added
    QStringList includedExtensions;     // optional override
    int         maxWorkerThreads        = 2;
    int         cpuTargetPct            = 30;
    int         cpuPauseThresholdPct    = 70;
    bool        pauseOnHeavyLoad        = true;
    bool        lazyOcrEnabled          = true;
    bool        hashLargeFiles          = true;
    bool        darkMode                = true;
    bool        monitorFileChanges      = true;
    QString     tessdataPath;           // empty = use default
    QString     ocrLanguage             = "eng";
    int         thumbnailSize           = 256;
    QString     lastBackupPath;
};

} // namespace DocuSearch
