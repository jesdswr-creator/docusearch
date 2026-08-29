#pragma once

// ============================================================
// PreviewPane.h - Document viewer with rich preview
// ============================================================
//
// Layout (top to bottom):
//   ┌──────────────────────────────────────────────────────┐
//   │ [icon] NOC.pdf  ‹ 1 /2 ›  − 100% +  [Open][OCR][⋯]  │  viewer header
//   ├──────────────────────────────────────────────────────┤
//   │                                                      │
//   │   ┌─────────────────────────────────────────┐       │
//   │   │                                         │       │
//   │   │   Document preview (image or text)      │       │
//   │   │                                         │       │
//   │   └─────────────────────────────────────────┘       │
//   │                                                      │
//   ├──────────────────────────────────────────────────────┤
//   │ Extracted Text | AI Summary | Highlights | Related  │  tabs
//   │ ...extracted content...                              │
//   │                                  [copy][↓]           │
//   └──────────────────────────────────────────────────────┘
//
// Preview modes:
//   - PDF: render each page to a QImage via PDFium, display as
//     a scrollable stack of page images.
//   - DOCX/XLSX/PPTX/TXT: display extracted text in a QTextBrowser
//     with basic formatting (bold headings, sheet/slide separators).
//   - Images (PNG/JPG): display the image directly.
// ============================================================

#include <QWidget>
#include <QLabel>
#include <QTextEdit>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QButtonGroup>
#include <QScrollArea>
#include <QImage>

namespace DocuSearch {

class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(QWidget* parent = nullptr);

    void setThumbnail(const QPixmap& pix);
    void setExtractedText(const QString& text);
    void setFilePath(const QString& path);
    void setPageInfo(int currentPage, int totalPages);
    void setDocumentText(const QString& text);
    void setSearchQuery(const QString& query);  // highlight search terms
    void clear();

    void refreshIcons();

signals:
    void openRequested(const QString& path);
    void ocrRequested(const QString& path);

private slots:
    void onOpenClicked();
    void onOcrClicked();
    void onPrevPage();
    void onNextPage();
    void onZoomIn();
    void onZoomOut();
    void onCopyExtracted();
    void onDownloadExtracted();
    void onTabClicked(int id);
    void onFitClicked();
    void onRotateClicked();
    void onMoreClicked();
    void onPageInputChanged();

private:
    // Viewer header
    QLabel*    fileIconLbl_       = nullptr;
    QLabel*    viewerTitle_       = nullptr;
    QPushButton* prevPageBtn_     = nullptr;
    QLineEdit* pageInput_         = nullptr;
    QLabel*    pageTotal_         = nullptr;
    QPushButton* nextPageBtn_     = nullptr;
    QPushButton* zoomOutBtn_      = nullptr;
    QLabel*    zoomLevel_         = nullptr;
    QPushButton* zoomInBtn_       = nullptr;
    QPushButton* openBtn_         = nullptr;
    QPushButton* ocrBtn_          = nullptr;
    QPushButton* moreBtn_         = nullptr;

    // Viewer body
    QScrollArea* previewScroll_   = nullptr;
    QLabel*      pageImageLbl_    = nullptr;  // for PDF/image preview
    QTextBrowser* documentPage_   = nullptr;  // for text preview

    // Extracted panel
    QPushButton* tabExtracted_    = nullptr;
    QPushButton* tabSummary_      = nullptr;
    QPushButton* tabHighlights_   = nullptr;
    QPushButton* tabRelated_      = nullptr;
    QTextEdit* extractedContent_  = nullptr;
    QPushButton* copyBtn_         = nullptr;
    QPushButton* downloadBtn_     = nullptr;

    // State
    int currentPage_  = 1;
    int totalPages_   = 1;
    int zoomPercent_  = 100;
    int rotation_     = 0;  // 0, 90, 180, 270
    QString currentPath_;
    QString currentExt_;
    QString currentExtracted_;
    QString currentDocumentText_;
    QString searchQuery_;  // current search query for highlighting
    QButtonGroup* tabGroup_ = nullptr;

    void updatePageDisplay();
    void updateZoomDisplay();
    void renderCurrentPage();
    void showTextPreview(const QString& text);
    void showPdfPreview();
    void showImagePreview(const QString& path);
    void setPreviewMode(bool showImage);  // true=image, false=text
    void highlightSearchTerms();
};

} // namespace DocuSearch
