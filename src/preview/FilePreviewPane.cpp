// ============================================================
// FilePreviewPane.cpp - File preview container
// ============================================================

#include "FilePreviewPane.h"

#include <QVBoxLayout>
#include <QFileInfo>
#include <QFile>
#include <QHBoxLayout>

namespace DocuSearch {

FilePreviewPane::FilePreviewPane(QWidget* parent)
    : QWidget(parent) {
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    m_headerLabel = new QLabel("", this);
    m_headerLabel->setStyleSheet(
        "font-weight: bold; font-size: 10pt; background: #e8e8e8; "
        "padding: 4px 8px; border-bottom: 1px solid #ccc;");
    mainLay->addWidget(m_headerLabel);

    m_stack = new QStackedWidget(this);

    m_pdfPreview    = new PdfPreview(this);
    m_imagePreview  = new ImagePreview(this);
    m_textPreview   = new TextPreview(this);
    m_officePreview = new OfficePreview(this);

    m_stack->addWidget(m_pdfPreview->getWidget());
    m_stack->addWidget(m_imagePreview->getWidget());
    m_stack->addWidget(m_textPreview->getWidget());
    m_stack->addWidget(m_officePreview->getWidget());

    // Unavailable widget
    m_unavailableWidget = new QWidget(this);
    auto* unLay = new QVBoxLayout(m_unavailableWidget);
    unLay->addStretch();
    m_unavailableLabel = new QLabel("Select a file to preview", m_unavailableWidget);
    m_unavailableLabel->setAlignment(Qt::AlignCenter);
    m_unavailableLabel->setStyleSheet("color: #999; font-size: 14pt;");
    unLay->addWidget(m_unavailableLabel);
    unLay->addStretch();
    m_stack->addWidget(m_unavailableWidget);

    m_stack->setCurrentWidget(m_unavailableWidget);
    mainLay->addWidget(m_stack, 1);
}

bool FilePreviewPane::loadFile(const QString& filePath) {
    if (filePath.isEmpty()) {
        clearPreview();
        return false;
    }
    if (!QFile::exists(filePath)) {
        showUnavailable("File not found:\n" + filePath);
        return false;
    }

    m_currentFilePath = filePath;
    const QString ext = QFileInfo(filePath).suffix().toLower();

    if (ext == "pdf") {
        m_headerLabel->setText("  PDF Document");
        m_pdfPreview->clear();
        const bool ok = m_pdfPreview->loadFile(filePath);
        m_stack->setCurrentWidget(m_pdfPreview->getWidget());
        return ok;
    }

    if (ext == "jpg" || ext == "jpeg" || ext == "png" ||
        ext == "bmp" || ext == "gif" || ext == "tiff" || ext == "tif" ||
        ext == "webp") {
        m_headerLabel->setText("  Image File");
        m_imagePreview->clear();
        const bool ok = m_imagePreview->loadFile(filePath);
        m_stack->setCurrentWidget(m_imagePreview->getWidget());
        return ok;
    }

    if (ext == "txt" || ext == "csv" || ext == "md" ||
        ext == "rtf" || ext == "log") {
        m_headerLabel->setText("  Text File");
        m_textPreview->clear();
        const bool ok = m_textPreview->loadFile(filePath);
        m_stack->setCurrentWidget(m_textPreview->getWidget());
        return ok;
    }

    if (ext == "docx" || ext == "doc" || ext == "pptx" ||
        ext == "ppt" || ext == "xlsx" || ext == "xls") {
        m_headerLabel->setText("  Office Document");
        m_officePreview->clear();
        const bool ok = m_officePreview->loadFile(filePath);
        m_stack->setCurrentWidget(m_officePreview->getWidget());
        return ok;
    }

    // Unsupported type
    showUnavailable(
        QString("Preview not available\n\nFile: %1\nType: .%2\nSize: %3 KB")
            .arg(QFileInfo(filePath).fileName())
            .arg(ext)
            .arg(QFileInfo(filePath).size() / 1024.0, 0, 'f', 1));
    return false;
}

void FilePreviewPane::clearPreview() {
    m_pdfPreview->clear();
    m_imagePreview->clear();
    m_textPreview->clear();
    m_officePreview->clear();
    m_headerLabel->setText("");
    m_currentFilePath.clear();
    showUnavailable("Select a file to preview");
}

void FilePreviewPane::showUnavailable(const QString& message) {
    m_unavailableLabel->setText(message);
    m_stack->setCurrentWidget(m_unavailableWidget);
}

bool FilePreviewPane::isPreviewable(const QString& filePath) {
    static const QStringList supported = {
        "pdf", "jpg", "jpeg", "png", "bmp", "gif", "tiff", "tif", "webp",
        "txt", "csv", "md", "rtf", "log",
        "docx", "doc", "pptx", "ppt", "xlsx", "xls"
    };
    return supported.contains(QFileInfo(filePath).suffix().toLower());
}

} // namespace DocuSearch
