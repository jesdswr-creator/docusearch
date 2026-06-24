#pragma once

// ============================================================
// ResultsPane.h — QTableWidget-based results list
// ============================================================

#include <QWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLabel>
#include <QList>
#include "../core/Types.h"

namespace DocuSearch {

class ResultsPane : public QWidget {
    Q_OBJECT
public:
    explicit ResultsPane(QWidget* parent = nullptr);

    void setResults(const QList<SearchHit>& hits);
    void appendResults(const QList<SearchHit>& hits);
    void clear();

    qint64 selectedFileId() const;
    QString selectedPath() const;

signals:
    void fileSelected(qint64 fileId, const QString& path);
    void fileActivated(qint64 fileId, const QString& path);

private slots:
    void onCellClicked(int row, int col);
    void onCellActivated(int row, int col);

private:
    enum Column { ColName = 0, ColType, ColSize, ColDate, ColSnippet, ColCount };

    QTableWidget* table_;
    QLabel*       countLabel_;
    QList<SearchHit> current_;

    void populateRow(int row, const SearchHit& h);
    QString colorForExtension(const QString& ext) const;
    QString humanizeSize(qint64 bytes) const;
    QString stripBoldTags(const QString& s) const;
};

} // namespace DocuSearch
