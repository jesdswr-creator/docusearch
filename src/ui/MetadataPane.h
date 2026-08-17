#pragma once

// ============================================================
// MetadataPane.h - File metadata display matching reference design
// ============================================================
//
// Layout (vertical):
//   ┌──────────────────────────────────────────────────┐
//   │ Metadata                                [edit]  │  header
//   ├──────────────────────────────────────────────────┤
//   │ [📄] File Name    NOC.pdf                        │  row
//   │ [📁] Path         D:/Documents/Official/NOC.pdf  │
//   │ [📄] Type         PDF Document                   │
//   │ [⬆]  Size         57.3 KB                        │
//   │ [📅] Created      20 Aug 2025 10:24 AM           │
//   │ [📅] Modified     20 Aug 2025 10:24 AM           │
//   │ [🔒] Hash (SHA)   a1b2c3d4e5f6g7h8i9j0...        │
//   │ [🔍] Index        Text + OCR                     │
//   │ [🖼] Content      Text + Image                   │
//   └──────────────────────────────────────────────────┘
// ============================================================

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include "../core/Types.h"

namespace DocuSearch {

class MetadataPane : public QWidget {
    Q_OBJECT
public:
    explicit MetadataPane(QWidget* parent = nullptr);

    void setRecord(const FileRecord& r);
    void refreshIcons();

private:
    // Header
    QLabel*     titleLbl_  = nullptr;
    QLabel*     infoIconLbl_ = nullptr;  // unused — kept for ABI compat
    QPushButton* editBtn_   = nullptr;

    // Metadata rows (each row has icon + label + value)
    QLabel* filename_  = nullptr;
    QLabel* path_      = nullptr;
    QLabel* type_      = nullptr;
    QLabel* size_      = nullptr;
    QLabel* created_   = nullptr;
    QLabel* modified_  = nullptr;
    QLabel* hash_      = nullptr;
    QLabel* indexLbl_  = nullptr;
    QLabel* content_   = nullptr;

    QString humanizeStatus(const QString& s) const;
    QString colorForStatus(const QString& s) const;
};

} // namespace DocuSearch
