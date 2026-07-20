#pragma once

// ============================================================
// PdfPreview.h - Preview widget for PDF files
// ============================================================
//
// Renders PDF pages one at a time using Poppler (cpp binding).
// Lazy rendering — only the current page is held in memory.
// Caps: max 30 pages, max 100 MB file size.
// ============================================================

#include "IFilePreview.h"

#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
#include <memory>

namespace DocuSearch {

class PdfPreview : public QWidget, public IFilePreview {
    Q_OBJECT
public:
    explicit PdfPreview(QWidget* parent = nullptr);

    bool loadFile(const QString& filePath) override;
    QWidget* getWidget() override { return this; }
    void clear() override;
    QString getTypeName() const override { return "PDF"; }

private slots:
    void onNextPage();
    void onPreviousPage();
    void onZoomIn();
    void onZoomOut();
    void onFitWindow();

private:
    void renderPage(int pageNumber);
    void updateButtonStates();

    // Poppler document is held as void* to avoid leaking the poppler
    // header into this class's interface. Internally we cast back to
    // std::unique_ptr<poppler::document>.
    std::shared_ptr<void> m_document;  // actually std::shared_ptr<poppler::document>

    QLabel*        m_pageCanvas   = nullptr;
    QScrollArea*   m_scrollArea   = nullptr;
    QLabel*        m_pageLabel    = nullptr;
    QPushButton*   m_prevButton   = nullptr;
    QPushButton*   m_nextButton   = nullptr;
    QPushButton*   m_zoomInButton = nullptr;
    QPushButton*   m_zoomOutButton= nullptr;
    QPushButton*   m_fitButton    = nullptr;

    int    m_currentPage  = 0;
    int    m_totalPages   = 0;
    double m_zoomLevel    = 1.0;

    static const int    MAX_PAGES           = 30;
    static constexpr double RENDER_DPI      = 150.0;
    static const qint64 MAX_FILE_SIZE_BYTES = 100LL * 1024 * 1024;  // 100 MB
};

} // namespace DocuSearch
