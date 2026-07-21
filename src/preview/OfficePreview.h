#pragma once

// ============================================================
// OfficePreview.h - Preview widget for Office documents
// ============================================================
//
// IMPORTANT:
//   - Does NOT use LibreOffice
//   - Does NOT use QProcess / COM / OLE / ActiveX
//   - Shows extracted text + file metadata + "Open externally" button
// ============================================================

#include "IFilePreview.h"

#include <QWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>

namespace DocuSearch {

class OfficePreview : public QWidget, public IFilePreview {
    Q_OBJECT
public:
    explicit OfficePreview(QWidget* parent = nullptr);

    bool loadFile(const QString& filePath) override;
    QWidget* getWidget() override { return this; }
    void clear() override;
    QString getTypeName() const override { return "Office"; }

private slots:
    void onOpenFile();

private:
    QLabel*          m_fileInfoLabel  = nullptr;
    QPlainTextEdit*  m_textEdit       = nullptr;
    QPushButton*     m_openButton     = nullptr;
    QLabel*          m_headerLabel    = nullptr;
    QString          m_currentFilePath;

    static constexpr int MAX_PREVIEW_CHARS = 50 * 1024;  // 50 KB text cap
};

} // namespace DocuSearch
