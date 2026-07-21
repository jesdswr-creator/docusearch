#pragma once

// ============================================================
// FilePreviewPane.h - Container that routes files to preview widgets
// ============================================================
//
// NOTE: Named FilePreviewPane (not PreviewPane) to avoid a name
// collision with the existing DocuSearch::PreviewPane in src/ui/.
// The existing PreviewPane handles the combined preview+extracted
// text area; FilePreviewPane is the new top-pane-only file preview.
//
// Layout:
//   ┌──────────────────────────────────────────┐
//   │  [Header: file type]                     │
//   ├──────────────────────────────────────────┤
//   │                                          │
//   │   QStackedWidget (one of):               │
//   │     - PdfPreview                         │
//   │     - ImagePreview                       │
//   │     - TextPreview                        │
//   │     - OfficePreview                      │
//   │     - "Preview not available" widget     │
//   │                                          │
//   └──────────────────────────────────────────┘
// ============================================================

#include "IFilePreview.h"
#include "PdfPreview.h"
#include "ImagePreview.h"
#include "TextPreview.h"
#include "OfficePreview.h"

#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QString>

namespace DocuSearch {

class FilePreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit FilePreviewPane(QWidget* parent = nullptr);

    // Loads a file into the appropriate preview widget. Returns true
    // on success, false on failure (file not found, unsupported type,
    // etc.). On failure, the unavailable widget is shown.
    bool loadFile(const QString& filePath);

    // Clears all preview widgets and shows the default unavailable message.
    void clearPreview();

    // Returns true if this file type is previewable.
    static bool isPreviewable(const QString& filePath);

private:
    void showUnavailable(const QString& message);

    QStackedWidget* m_stack           = nullptr;
    PdfPreview*     m_pdfPreview      = nullptr;
    ImagePreview*   m_imagePreview    = nullptr;
    TextPreview*    m_textPreview     = nullptr;
    OfficePreview*  m_officePreview   = nullptr;
    QWidget*        m_unavailableWidget = nullptr;
    QLabel*         m_unavailableLabel  = nullptr;
    QLabel*         m_headerLabel       = nullptr;
    QString         m_currentFilePath;
};

} // namespace DocuSearch
