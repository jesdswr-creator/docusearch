#pragma once

#include <QWidget>
#include <QListWidget>
#include <QListWidgetItem>
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
    void onItemClicked(QListWidgetItem* item);
    void onItemActivated(QListWidgetItem* item);

private:
    QListWidget* list_;
    QLabel*      countLabel_;
    QList<SearchHit> current_;
};

} // namespace DocuSearch
