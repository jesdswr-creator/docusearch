#pragma once

// ============================================================
// MetadataPane.h - File metadata display (scrollable)
// ============================================================

#include <QWidget>
#include <QLabel>
#include <QFormLayout>
#include "../core/Types.h"

namespace DocuSearch {

class MetadataPane : public QWidget {
    Q_OBJECT
public:
    explicit MetadataPane(QWidget* parent = nullptr);

    void setRecord(const FileRecord& r);

private:
    QLabel* filename_;
    QLabel* path_;
    QLabel* ext_;
    QLabel* size_;
    QLabel* created_;
    QLabel* modified_;
    QLabel* hash_;
    QLabel* status_;
    QLabel* ocrStatus_;

    QString humanizeStatus(const QString& s) const;
    QString colorForStatus(const QString& s) const;
    void styleLabel(QLabel* lbl);
};

} // namespace DocuSearch
