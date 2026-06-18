#pragma once

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
};

} // namespace DocuSearch
