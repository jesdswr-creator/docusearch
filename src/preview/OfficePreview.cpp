// ============================================================
// OfficePreview.cpp - Office document preview (text + metadata)
// ============================================================

#include "OfficePreview.h"
#include "../documents/DocumentExtractorRegistry.h"
#include "../core/Config.h"
#include "../core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFont>
#include <QDesktopServices>
#include <sqlite3.h>
#include <QUrl>
#include <QDateTime>

namespace DocuSearch {

OfficePreview::OfficePreview(QWidget* parent)
    : QWidget(parent) {
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(4, 4, 4, 4);
    mainLay->setSpacing(4);

    m_headerLabel = new QLabel("Office Document Preview", this);
    m_headerLabel->setObjectName("previewHeader");
    mainLay->addWidget(m_headerLabel);

    m_fileInfoLabel = new QLabel(this);
    m_fileInfoLabel->setObjectName("previewInfo");
    m_fileInfoLabel->setWordWrap(true);
    mainLay->addWidget(m_fileInfoLabel);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    // No setStyleSheet — QFrame::HLine draws its own line via palette.
    mainLay->addWidget(sep);

    auto* note = new QLabel(
        "Showing extracted text content.\n"
        "For full visual preview, open the file externally.",
        this);
    note->setObjectName("previewNote");
    mainLay->addWidget(note);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    QFont monoFont(QStringLiteral("Courier New"), 9);
    monoFont.setStyleHint(QFont::Monospace);
    m_textEdit->setFont(monoFont);
    mainLay->addWidget(m_textEdit, 1);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_openButton = new QPushButton("Open with default application", this);
    btnRow->addWidget(m_openButton);
    btnRow->addStretch();
    mainLay->addLayout(btnRow);

    connect(m_openButton, &QPushButton::clicked, this, &OfficePreview::onOpenFile);
}

bool OfficePreview::loadFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;
    if (!QFile::exists(filePath)) {
        m_fileInfoLabel->setText("File not found: " + filePath);
        m_textEdit->clear();
        m_currentFilePath.clear();
        return false;
    }

    m_currentFilePath = filePath;

    try {
        QFileInfo fi(filePath);
        const QString ext = fi.suffix().toUpper();
        QString sizeStr;
        const qint64 size = fi.size();
        if (size < 1024 * 1024) {
            sizeStr = QString::number(size / 1024.0, 'f', 1) + " KB";
        } else {
            sizeStr = QString::number(size / (1024.0 * 1024.0), 'f', 2) + " MB";
        }
        const QString modDate = fi.lastModified().toString("yyyy-MM-dd hh:mm:ss");

        m_fileInfoLabel->setText(
            QString("File: %1\nType: %2 Document\nSize: %3\nModified: %4")
                .arg(fi.fileName()).arg(ext).arg(sizeStr).arg(modDate));

        // MED-3 fix: First try to read extracted text from the DocumentText
        // table (instant — no re-extraction needed). Only fall back to live
        // extraction if the file hasn't been indexed yet.
        QString text;
        bool gotFromDb = false;
        try {
            // Look up the file in the Files table by path, then read its
            // extracted text from DocumentText. This avoids the 2-5 second
            // re-extraction cost on every click.
            // Note: We use a fresh sqlite3 connection here (not the main
            // app's Database) to avoid thread-safety issues with the main
            // connection. The DB path comes from Config.
            const QString dbPath = Config::instance().dbPath();
            sqlite3* db = nullptr;
            if (sqlite3_open_v2(dbPath.toUtf8().constData(), &db,
                SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) == SQLITE_OK) {
                sqlite3_stmt* stmt = nullptr;
                // Find file_id by path, then get extracted_text.
                const char* sql =
                    "SELECT dt.extracted_text FROM DocumentText dt "
                    "JOIN Files f ON dt.file_id = f.id "
                    "WHERE f.path = ?1 LIMIT 1;";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    const QByteArray pathBytes = filePath.toUtf8();
                    sqlite3_bind_text(stmt, 1, pathBytes.constData(),
                                      pathBytes.size(), SQLITE_TRANSIENT);
                    if (sqlite3_step(stmt) == SQLITE_ROW) {
                        const unsigned char* txt = sqlite3_column_text(stmt, 0);
                        if (txt) {
                            text = QString::fromUtf8(
                                reinterpret_cast<const char*>(txt));
                            gotFromDb = !text.isEmpty();
                        }
                    }
                    sqlite3_finalize(stmt);
                }
                sqlite3_close(db);
            }
        } catch (...) {
            // DB lookup failure is non-fatal — fall through to live extraction.
        }

        // Fall back to live extraction only if DB didn't have it.
        if (!gotFromDb) {
            auto& registry = DocumentExtractorRegistry::instance();
            auto result = registry.extractByExtension(filePath, fi.suffix().toLower());
            text = result.text;
        }

        if (text.length() > MAX_PREVIEW_CHARS) {
            text = text.left(MAX_PREVIEW_CHARS);
            text += "\n\n... [Preview limited to first 50 KB] ...";
        }
        if (text.isEmpty()) {
            text = "[No text could be extracted from this file]\n\n"
                   "The file may be password-protected, binary,\n"
                   "or in an unsupported format.";
        }
        m_textEdit->setPlainText(text);
        return true;
    } catch (const std::exception& e) {
        m_fileInfoLabel->setText(QString("Error: %1").arg(e.what()));
        m_textEdit->setPlainText("[Error extracting text from this file]");
        DS_WARN("Preview", QString("OfficePreview exception: %1").arg(e.what()));
        return false;
    } catch (...) {
        m_fileInfoLabel->setText("Unknown error.");
        m_textEdit->setPlainText("[Unknown error extracting text]");
        return false;
    }
}

void OfficePreview::onOpenFile() {
    if (m_currentFilePath.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentFilePath));
}

void OfficePreview::clear() {
    m_currentFilePath.clear();
    m_fileInfoLabel->setText("");
    m_textEdit->clear();
}

} // namespace DocuSearch
