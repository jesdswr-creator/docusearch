#pragma once

// ============================================================
// SearchEngine.h — Run parsed queries against SQLite FTS5
// ============================================================

#include <QObject>
#include <QString>
#include <QList>
#include "../core/Types.h"

namespace DocuSearch {

class Database;
class FileRepository;

class SearchEngine : public QObject {
    Q_OBJECT
public:
    SearchEngine(Database& db, FileRepository& repo, QObject* parent = nullptr);

    // Execute a raw user query. Runs synchronously but fast (<100ms target).
    // Limit caps the number of returned hits.
    QList<SearchHit> search(const QString& rawQuery, int limit = 200);

    // Filename-only fast search (used for "Everything"-style instant search).
    QList<SearchHit> searchByFilename(const QString& term, int limit = 200);

    // Suggestions for "saved searches" autocomplete.
    QStringList recentSearches(int max = 20) const;
    void        recordSearch(const QString& q);

signals:
    void resultsReady(const QList<SearchHit>& hits);

private:
    Database&       db_;
    FileRepository& repo_;
};

} // namespace DocuSearch
