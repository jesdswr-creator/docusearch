#pragma once

// ============================================================
// PreviewPane.h - Document viewer matching reference design
// ============================================================
//
// Layout (top to bottom):
//   ┌──────────────────────────────────────────────────┐
//   │ NOC.pdf  ‹ 1 /2 ›  − 100% +   [⤢][↻][⋯]          │  viewer header
//   ├──────────────────────────────────────────────────┤
//   │                  ┌─────────────┐                 │
//   │                  │ Document    │                 │
//   │                  │ page text   │  (page          │
//   │                  │             │   thumbnails    │
//   │                  │             │   on right)     │
//   │                  └─────────────┘                 │
//   ├──────────────────────────────────────────────────┤
//   │ Extracted Text | AI Summary | Highlights | Rel.  │  tabs
//   │ ...extracted content...                          │
//   │                                  [copy][↓]       │
//   └──────────────────────────────────────────────────┘
// ============================================================

#include <QWidget>
#include <QLabel>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QPixmap>
#include <QButtonGroup>

// QButtonGroup is part of QtWidgets (no separate header since Qt 6).

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

private:
    // Viewer header
    QLabel*    viewerTitle_       = nullptr;
    QPushButton* prevPageBtn_     = nullptr;
    QLineEdit* pageInput_         = nullptr;
    QLabel*    pageTotal_         = nullptr;
    QPushButton* nextPageBtn_     = nullptr;
    QPushButton* zoomOutBtn_      = nullptr;
    QLabel*    zoomLevel_         = nullptr;
    QPushButton* zoomInBtn_       = nullptr;
    QPushButton* fitBtn_          = nullptr;
    QPushButton* rotateBtn_       = nullptr;
    QPushButton* moreBtn_         = nullptr;

    // Viewer body
    QTextEdit* documentPage_      = nullptr;

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
    QString currentPath_;
    QString currentExtracted_;
    QButtonGroup* tabGroup_ = nullptr;

    void updatePageDisplay();
    void updateZoomDisplay();
};

} // namespace DocuSearch
