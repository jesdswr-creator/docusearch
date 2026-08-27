#pragma once

// ============================================================
// PdfPreview.h - Continuous scrolling PDF preview (Poppler)
// ============================================================
//
// All pages are laid out vertically inside one scroll area so the
// user reads the document like in a normal PDF viewer (scroll wheel,
// Prev/Next snap to page boundaries). Pages are rendered LAZILY:
// only pages near the viewport are rasterized, and far-away pixmaps
// are evicted, keeping memory bounded even for 30-page documents.
// Caps: max 30 pages, max 100 MB file size.
// ============================================================

#include "IFilePreview.h"

#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
#include <QVector>
#include <QSize>
#include <QTimer>
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
    void refreshIcons();          // re-render toolbar glyphs after retheme

protected:
    // Reimplemented to call onFitWindow() the first time the widget
    // is shown — at that point the scroll area has its final viewport
    // size, so the zoom calculation is accurate.
    void showEvent(QShowEvent* event) override;

private slots:
    void onNextPage();
    void onPreviousPage();
    void onZoomIn();
    void onZoomOut();
    void onFitWindow();
    void onScrolled();

private:
    QSize   pageSizePixels(int index) const;
    void    measureBaseSizes();           // low-DPI probe of true page sizes
    void    rebuildPages();               // (re)create placeholder labels
    void    applyZoom(double newZoom);    // relayout + re-render visible set
    int     currentPageFromScrollPos() const;
    void    updatePageIndicator();
    void    updateButtonStates();
    void    scheduleRenderPass();         // debounced renderVisiblePages()
    void    renderVisiblePages();         // rasterize viewport-neighbourhood
    bool    renderIntoLabel(int index);   // sync-render one page pixmap
    void    scrollToPage(int index);
    void    pumpRenderQueue();            // step the sequential render queue

    std::shared_ptr<void> m_document;     // poppler::document (type-erased)

    QWidget*       m_pagesHost      = nullptr;
    QLabel*        m_pageLabel      = nullptr;
    QScrollArea*   m_scrollArea     = nullptr;
    QVector<QLabel*> m_pageLabels;
    QVector<QSize>   m_baseSizes;         // pixels at zoom level 1
    QPushButton*   m_prevButton     = nullptr;
    QPushButton*   m_nextButton     = nullptr;
    QPushButton*   m_zoomInButton   = nullptr;
    QPushButton*   m_zoomOutButton  = nullptr;
    QPushButton*   m_fitButton      = nullptr;

    QTimer         m_renderDebounce;      // coalesces scroll/zoom bursts
    QVector<int>   m_renderQueue;         // page indices pending rasterization
    bool           m_pumping        = false;

    int    m_currentPage  = 0;
    int    m_totalPages   = 0;
    double m_zoomLevel    = 1.0;
    bool   m_pendingFit   = false;   // true = need to call onFitWindow on first show

    static const int    MAX_PAGES           = 30;
    static constexpr double RENDER_DPI      = 150.0;
    static const qint64 MAX_FILE_SIZE_BYTES = 100LL * 1024 * 1024;  // 100 MB
};

} // namespace DocuSearch
