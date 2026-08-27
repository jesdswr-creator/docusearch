// ============================================================
// PdfPreview.cpp - Continuous scrolling PDF preview (Poppler)
// ============================================================
//
// The document is rendered as a vertical stack of page "cards"
// inside a scroll area — like a normal PDF reader. Pages outside
// the viewport are rendered lazily by a debounced pass driven by
// the scrollbar, and pixmaps far away from the viewport are evicted
// so memory stays bounded (a 150-DPI A4 page is ~8 MB of pixels).
// ============================================================

#include "PdfPreview.h"
#include "../core/Logger.h"
#include "../ui/IconUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QApplication>
#include <QPalette>
#include <algorithm>

#ifdef DOCUSEARCH_HAS_POPPLER
#  include <poppler-document.h>
#  include <poppler-page.h>
#  include <poppler-page-renderer.h>
#  include <poppler-image.h>
#endif

namespace DocuSearch {

namespace {
// How many pages beyond the visible range to keep rasterized.
constexpr int kRenderContextPages = 1;
// Hard cap on pixmaps kept alive; anything farther than this many
// pages from the viewport is evicted during the render pass.
constexpr int kEvictDistance      = 8;
} // namespace

PdfPreview::PdfPreview(QWidget* parent)
    : QWidget(parent) {
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // Toolbar — styling via global QSS (#previewToolbar)
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName("previewToolbar");
    auto* tbLay = new QHBoxLayout(toolbar);
    tbLay->setContentsMargins(6, 4, 6, 4);
    tbLay->setSpacing(4);

    m_prevButton    = new QPushButton("‹ Prev", toolbar);
    m_prevButton->setToolTip("Go to the previous page");
    m_nextButton    = new QPushButton("Next ›", toolbar);
    m_nextButton->setToolTip("Go to the next page");
    m_zoomOutButton = new QPushButton(toolbar);
    m_zoomOutButton->setObjectName("previewIconBtn");
    m_zoomOutButton->setFixedSize(30, 28);
    m_zoomOutButton->setToolTip("Zoom out");
    m_zoomInButton  = new QPushButton(toolbar);
    m_zoomInButton->setObjectName("previewIconBtn");
    m_zoomInButton->setFixedSize(30, 28);
    m_zoomInButton->setToolTip("Zoom in");
    m_fitButton     = new QPushButton("Fit", toolbar);
    m_fitButton->setToolTip("Fit the page width to the window");

    m_pageLabel = new QLabel("No document loaded", toolbar);
    m_pageLabel->setObjectName("previewPageLabel");

    tbLay->addWidget(m_prevButton);
    tbLay->addWidget(m_nextButton);
    tbLay->addSpacing(10);
    tbLay->addWidget(m_zoomOutButton);
    tbLay->addWidget(m_zoomInButton);
    tbLay->addWidget(m_fitButton);
    tbLay->addSpacing(12);
    tbLay->addWidget(m_pageLabel, 1);

    mainLay->addWidget(toolbar);

    // Scroll area hosting the continuous page stack.
    // NOTE: deliberately NO setAlignment() here — with widgetResizable
    // enabled, an alignment flag makes QScrollArea shrink the host widget
    // to its sizeHint instead of stretching it to the viewport width,
    // which collapses the whole page column.
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setObjectName("previewScrollDark");

    m_pagesHost = new QWidget(m_scrollArea);
    m_pagesHost->setObjectName("pdfPagesHost");
    auto* hostLay = new QVBoxLayout(m_pagesHost);
    hostLay->setContentsMargins(10, 10, 10, 14);
    hostLay->setSpacing(10);
    hostLay->addStretch();
    m_scrollArea->setWidget(m_pagesHost);

    mainLay->addWidget(m_scrollArea, 1);

    connect(m_prevButton,    &QPushButton::clicked, this, &PdfPreview::onPreviousPage);
    connect(m_nextButton,    &QPushButton::clicked, this, &PdfPreview::onNextPage);
    connect(m_zoomInButton,  &QPushButton::clicked, this, &PdfPreview::onZoomIn);
    connect(m_zoomOutButton, &QPushButton::clicked, this, &PdfPreview::onZoomOut);
    connect(m_fitButton,     &QPushButton::clicked, this, &PdfPreview::onFitWindow);

    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &PdfPreview::onScrolled);

    // Coalesce rapid scroll/zoom events into one render pass.
    m_renderDebounce.setSingleShot(true);
    m_renderDebounce.setInterval(120);
    connect(&m_renderDebounce, &QTimer::timeout,
            this, &PdfPreview::renderVisiblePages);

    refreshIcons();
    updateButtonStates();
}

void PdfPreview::refreshIcons() {
    const QColor textColor = qApp->palette().color(QPalette::Text);
    m_zoomInButton->setIcon(loadLucideIcon("zoom-in", textColor, 15));
    m_zoomOutButton->setIcon(loadLucideIcon("zoom-out", textColor, 15));
    m_zoomInButton->setIconSize(QSize(15, 15));
    m_zoomOutButton->setIconSize(QSize(15, 15));
}

bool PdfPreview::loadFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;
    if (!QFile::exists(filePath)) {
        // Clear any old pages before showing the error.
        clear();
        m_pageLabel->setText("File not found");
        return false;
    }

    const qint64 fileSize = QFileInfo(filePath).size();
    if (fileSize > MAX_FILE_SIZE_BYTES) {
        clear();
        m_pageLabel->setText("PDF too large (>100 MB)");
        return false;
    }

#ifdef DOCUSEARCH_HAS_POPPLER
    try {
        // poppler::document::load_from_file returns a raw pointer that
        // we own. Wrap it in shared_ptr<void> with a custom deleter.
        // Note: For password-protected PDFs, load_from_file returns nullptr.
        // We try with empty owner/user passwords as a fallback (some PDFs
        // have an empty owner password that unlocks them).
        const std::string stdPath = filePath.toStdString();
        poppler::document* docRaw = poppler::document::load_from_file(stdPath);
        if (!docRaw) {
            docRaw = poppler::document::load_from_file(stdPath, "", "");
        }
        if (!docRaw || docRaw->pages() == 0) {
            delete docRaw;
            clear();
            m_pageLabel->setText("Cannot open PDF");
            return false;
        }

        const int realPages = docRaw->pages();
        m_totalPages = std::min(realPages, MAX_PAGES);

        m_document = std::shared_ptr<void>(
            docRaw,
            [](void* p) { delete static_cast<poppler::document*>(p); });

        m_currentPage = 0;
        m_zoomLevel   = 1.0;

        rebuildPages();
        m_pendingFit = true;  // trigger onFitWindow() in showEvent

        // First paint: even if showEvent does not fire (pane already
        // visible), give the viewport an initial render pass.
        scheduleRenderPass();

        return true;
    } catch (const std::exception& e) {
        DS_WARN("Preview", QString("PdfPreview load exception: %1").arg(e.what()));
        clear();
        m_pageLabel->setText("Error opening PDF");
        return false;
    } catch (...) {
        clear();
        m_pageLabel->setText("Unknown error opening PDF");
        return false;
    }
#else
    clear();
    return false;
#endif
}

QSizeF PdfPreview::pageSizePoints(int index) const {
#ifdef DOCUSEARCH_HAS_POPPLER
    if (!m_document || index < 0 || index >= m_totalPages) return QSizeF();
    auto* doc = static_cast<poppler::document*>(m_document.get());
    poppler::page* page = doc->create_page(index);
    if (!page) return QSizeF();
    const poppler::rectf r = page->dimensions();
    delete page;
    return QSizeF(r.width(), r.height());
#else
    Q_UNUSED(index);
    return QSizeF();
#endif
}

QSize PdfPreview::pageSizePixels(int index) const {
    const QSizeF pts = pageSizePoints(index);
    if (pts.isEmpty()) return QSize(600, 800);  // sane fallback aspect
    const double scale = RENDER_DPI * m_zoomLevel / 72.0;
    return QSize(qMax(64, int(pts.width()  * scale + 0.5)),
                 qMax(84, int(pts.height() * scale + 0.5)));
}

void PdfPreview::rebuildPages() {
    auto* lay = qobject_cast<QBoxLayout*>(m_pagesHost->layout());

    // Drop all existing page labels.
    while (lay && lay->count() > 1) {          // [0..n-1] labels + trailing stretch
        QLayoutItem* it = lay->takeAt(lay->count() - 2);
        if (auto* w = it->widget()) w->deleteLater();
        delete it;
    }
    m_pageLabels.clear();
    m_renderQueue.clear();
    m_pumping = false;

#ifdef DOCUSEARCH_HAS_POPPLER
    for (int i = 0; i < m_totalPages; ++i) {
        auto* lbl = new QLabel(m_pagesHost);
        lbl->setObjectName("pdfPageCard");
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setText(QString("Page %1").arg(i + 1));
        lbl->setScaledContents(false);
        // Reserve the exact final size so scroll offsets are stable
        // before the pixmap arrives (lazy placeholder).
        const QSize px = [&]{
            const QSizeF pts = pageSizePoints(i);
            if (pts.isEmpty()) return QSize(600, 800);
            const double scale = RENDER_DPI * m_zoomLevel / 72.0;
            return QSize(qMax(64, int(pts.width()  * scale + 0.5)),
                         qMax(84, int(pts.height() * scale + 0.5)));
        }();
        lbl->setFixedSize(px);
        lay->insertWidget(lay->count() - 1, lbl, 0, Qt::AlignHCenter);
        m_pageLabels.append(lbl);
    }
#endif

    updatePageIndicator();
    updateButtonStates();
}

void PdfPreview::applyZoom(double newZoom) {
    if (m_totalPages <= 0) return;
    newZoom = std::min(2.0, std::max(0.1, newZoom));

    // Preserve reading position relative to the current page.
    QLabel* cur = (m_currentPage >= 0 && m_currentPage < m_pageLabels.size())
                      ? m_pageLabels[m_currentPage] : nullptr;
    const int pageTop     = cur ? cur->y() : 0;
    const int offsetInOld = m_scrollArea->verticalScrollBar()->value() - pageTop;
    const int oldHeight   = cur ? cur->height() : 1;

    m_zoomLevel = newZoom;

    for (int i = 0; i < m_pageLabels.size(); ++i) {
        m_pageLabels[i]->setPixmap(QPixmap());  // evict cached rendering
        m_pageLabels[i]->setText(QString("Page %1").arg(i + 1));
        m_pageLabels[i]->setFixedSize(pageSizePixels(i));
    }
    m_renderQueue.clear();
    m_pumping = false;

    // Re-anchor on the same page at roughly the same relative position.
    QTimer::singleShot(0, this, [this, pageTop, offsetInOld, oldHeight]() {
        QLabel* c = (m_currentPage >= 0 && m_currentPage < m_pageLabels.size())
                        ? m_pageLabels[m_currentPage] : nullptr;
        if (c) {
            const int hNew = c->height();
            m_scrollArea->verticalScrollBar()->setValue(
                c->y() + (hNew > 0 ? int(offsetInOld * double(hNew) / oldHeight) : 0));
        } else {
            (void)pageTop;
        }
        scheduleRenderPass();
    });
}

void PdfPreview::scheduleRenderPass() {
    m_renderDebounce.start();
}

void PdfPreview::onScrolled() {
    const int prev = m_currentPage;
    m_currentPage = currentPageFromScrollPos();
    if (prev != m_currentPage) updateButtonStates();
    updatePageIndicator();
    scheduleRenderPass();
}

int PdfPreview::currentPageFromScrollPos() const {
    if (m_totalPages <= 0) return 0;
    const int pos = m_scrollArea->verticalScrollBar()->value()
                  + m_scrollArea->viewport()->height() / 3;
    int best = 0;
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        if (m_pageLabels[i]->y() <= pos) best = i; else break;
    }
    return best;
}

void PdfPreview::updatePageIndicator() {
    if (m_totalPages <= 0) { m_pageLabel->setText("No document loaded"); return; }
    m_pageLabel->setText(QString("Page %1 of %2")
                             .arg(m_currentPage + 1).arg(m_totalPages));
}

bool PdfPreview::renderIntoLabel(int index) {
#ifdef DOCUSEARCH_HAS_POPPLER
    if (!m_document || index < 0 || index >= m_totalPages) return false;
    try {
        auto* doc = static_cast<poppler::document*>(m_document.get());
        poppler::page* page = doc->create_page(index);
        if (!page) return false;

        poppler::page_renderer renderer;
        renderer.set_render_hint(poppler::page_renderer::text_antialiasing);

        const double dpi = RENDER_DPI * m_zoomLevel;
        auto img_data = renderer.render_page(page, dpi, dpi);
        delete page;
        page = nullptr;

        if (!img_data.is_valid() || !img_data.data()) return false;

        QImage qimg(reinterpret_cast<const uchar*>(img_data.data()),
                    img_data.width(), img_data.height(),
                    img_data.bytes_per_row(),
                    QImage::Format_ARGB32);
        if (qimg.isNull()) return false;

        if (index >= m_pageLabels.size()) return false;
        QLabel* lbl = m_pageLabels[index];
        lbl->setPixmap(QPixmap::fromImage(qimg.copy()));
        lbl->setText(QString());
        return true;
    } catch (const std::exception& e) {
        DS_WARN("Preview", QString("PdfPreview render exception: %1").arg(e.what()));
        return false;
    } catch (...) {
        return false;
    }
#else
    Q_UNUSED(index);
    return false;
#endif
}

void PdfPreview::renderVisiblePages() {
    if (m_totalPages <= 0 || m_pageLabels.isEmpty()) return;

    const int first = m_currentPage;
    const int last  = qBound(0, currentPageFromScrollPos(), m_totalPages - 1);
    Q_UNUSED(first);

    // Determine visible window using label geometry (cheap).
    int visFirst = last, visLast = last;
    // currentPageFromScrollPos already reflects the viewport window.

    // Queue missing pages around the viewport.
    for (int i = std::max(0, visFirst - kRenderContextPages);
         i <= std::min(m_totalPages - 1, visLast + kRenderContextPages); ++i) {
        if (!m_renderQueue.contains(i)) m_renderQueue.append(i);
    }
    pumpRenderQueue();

    // Evict far-away pixmaps to cap memory (~8 MB per A4 page @150 DPI).
    const int span = kEvictDistance;
    const int cacheFirst = std::max(0, last - span);
    const int cacheLast  = std::min(m_totalPages - 1, last + span);
    for (int i = 0; i < m_pageLabels.size(); ++i) {
        if (i < cacheFirst || i > cacheLast) {
            if (!m_pageLabels[i]->pixmap().isNull()) {
                m_pageLabels[i]->setPixmap(QPixmap());
                m_pageLabels[i]->setText(QString("Page %1").arg(i + 1));
            }
        }
    }
}

void PdfPreview::pumpRenderQueue() {
    if (m_pumping) return;
    // Skip entries that are already rasterized or out of range.
    while (!m_renderQueue.isEmpty()) {
        const int idx = m_renderQueue.front();
        const bool done = idx < m_pageLabels.size()
            && !m_pageLabels[idx]->pixmap().isNull();
        if (done) { m_renderQueue.pop_front(); continue; }
        break;
    }
    if (m_renderQueue.isEmpty()) return;

    m_pumping = true;
    const int idx = m_renderQueue.takeFirst();
    renderIntoLabel(idx);
    m_pumping = false;

    if (!m_renderQueue.isEmpty())
        QTimer::singleShot(15, this, &PdfPreview::pumpRenderQueue);
}

void PdfPreview::onNextPage()     { scrollToPage(m_currentPage + 1); }
void PdfPreview::onPreviousPage() { scrollToPage(m_currentPage - 1); }

void PdfPreview::scrollToPage(int index) {
    if (index < 0 || index >= m_pageLabels.size()) return;
    m_scrollArea->ensureWidgetVisible(m_pageLabels[index], 4, 4);
    m_currentPage = index;
    updatePageIndicator();
    updateButtonStates();
    scheduleRenderPass();
}

void PdfPreview::onZoomIn()  { applyZoom(m_zoomLevel * 1.25); }
void PdfPreview::onZoomOut() { applyZoom(std::max(0.1, m_zoomLevel / 1.25)); }

void PdfPreview::onFitWindow() {
    if (!m_document || m_totalPages == 0) return;
    // Fit the WIDEST page to the viewport width so no horizontal
    // scrolling surprises appear mid-document.
    double widestPts = 0.0;
    for (int i = 0; i < m_totalPages; ++i) {
        const QSizeF pts = pageSizePoints(i);
        widestPts = std::max(widestPts, pts.width());
    }
    const int viewportWidth = m_scrollArea->viewport()->width() - 24 - 20; // margins+scrollbar
    if (widestPts <= 0.0 || viewportWidth <= 0) {
        applyZoom(1.0);
        return;
    }
    // pixelWidth = ptsWidth * DPI/72 * zoom  =>  zoom = target/pw at zoom 1.
    const double pxAtZoom1 = widestPts * RENDER_DPI / 72.0;
    applyZoom(viewportWidth / pxAtZoom1);
}

void PdfPreview::clear() {
    m_document.reset();
    m_totalPages  = 0;
    m_currentPage = 0;
    m_zoomLevel   = 1.0;
    m_pendingFit  = false;
    rebuildPages();                 // removes all page cards
    m_pageLabel->setText("No document loaded");
    updateButtonStates();
}

void PdfPreview::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // If loadFile() requested a fit-to-window call (m_pendingFit=true)
    // and we now have a real viewport size, do the fit. This runs on
    // the first showEvent after loadFile() — at that point the layout
    // has settled and viewport()->width() returns the real width.
    if (m_pendingFit && m_document && m_totalPages > 0) {
        m_pendingFit = false;
        onFitWindow();
    } else if (m_document && m_totalPages > 0) {
        scheduleRenderPass();  // re-check visible set after re-show
    }
}

void PdfPreview::updateButtonStates() {
    m_prevButton->setEnabled(m_currentPage > 0 && m_totalPages > 0);
    m_nextButton->setEnabled(m_currentPage < m_totalPages - 1 && m_totalPages > 0);
    m_zoomInButton->setEnabled(m_totalPages > 0);
    m_zoomOutButton->setEnabled(m_totalPages > 0);
    m_fitButton->setEnabled(m_totalPages > 0);
}

} // namespace DocuSearch
