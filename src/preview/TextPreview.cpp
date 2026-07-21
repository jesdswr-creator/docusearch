// ============================================================
// TextPreview.cpp - Plain-text file preview
// ============================================================

#include "TextPreview.h"

#include <QFile>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QFont>

namespace DocuSearch {

TextPreview::TextPreview(QWidget* parent)
    : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet(
        "color: #666; font-size: 10pt; padding: 4px 8px; "
        "background: #f5f5f5; border-bottom: 1px solid #ddd;");
    lay->addWidget(m_infoLabel);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    QFont monoFont(QStringLiteral("Courier New"), 10);
    monoFont.setStyleHint(QFont::Monospace);
    m_textEdit->setFont(monoFont);
    m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    lay->addWidget(m_textEdit, 1);
}

bool TextPreview::loadFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;
    if (!QFile::exists(filePath)) {
        m_textEdit->setPlainText("File not found: " + filePath);
        m_infoLabel->setText("");
        return false;
    }

    try {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_textEdit->setPlainText("Cannot open file: " + filePath);
            return false;
        }
        QByteArray content = file.read(MAX_BYTES);
        file.close();

        const qint64 fileSize = QFileInfo(filePath).size();
        const bool truncated = (fileSize > MAX_BYTES);

        QString text = QString::fromUtf8(content);
        if (truncated) {
            text += "\n\n... [Preview limited to first 50 KB] ...";
        }
        m_textEdit->setPlainText(text);

        m_infoLabel->setText(QString("%1  |  %2 KB")
            .arg(QFileInfo(filePath).fileName())
            .arg(fileSize / 1024.0, 0, 'f', 1));
        return true;
    } catch (const std::exception& e) {
        m_textEdit->setPlainText(QString("Error loading file: %1").arg(e.what()));
        return false;
    } catch (...) {
        m_textEdit->setPlainText("Unknown error loading file.");
        return false;
    }
}

void TextPreview::clear() {
    m_textEdit->clear();
    m_infoLabel->setText("");
}

} // namespace DocuSearch
