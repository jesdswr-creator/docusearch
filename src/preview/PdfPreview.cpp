// ============================================================
// PdfPreview.cpp - PDF page-by-page preview using Poppler
// ============================================================

#include "PdfPreview.h"
#include "../core/Logger.h"

#ifdef DOCUSEARCH_HAS_POPPLER
#  include <poppler-document.h>
#  include <poppler-page.h>
#  include <poppler-page-renderer.h>
#  include <poppler-image.h>
#endif

#include <QFile>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QImage>
#include <chrono>

namespace DocuSearch {

PdfPreview::PdfPreview(QWidget* parent)
    : QWidget(parent) {
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // Toolbar
    auto* toolbar = new QWidget(this);
    auto* tbLay = new QHBoxLayout(toolbar);
    tbLay->setContentsMargins(4, 4, 4, 4);
    tbLay->setSpacing(4);

    m_prevButton    = new QPushButton("‹ Prev", toolbar);
    m_nextButton    = new QPushButton("Next ›", toolbar);
    m_zoomOutButton = new QPushButton("-", toolbar);
    m_zoomOutButton->setFixedWidth(32);
    m_zoomInButton  = new QPushButton("+", toolbar);
    m_zoomInButton->setFixedWidth(32);
    m_fitButton     = new QPushButton("Fit", toolbar);

    m_pageLabel = new QLabel("No document loaded", toolbar);
    m_pageLabel->setStyleSheet("color: #666; padding: 0 8px;");

    tbLay->addWidget(m_prevButton);
    tbLay->addWidget(m_nextButton);
    tbLay->addSpacing(12);
    tbLay->addWidget(m_zoomOutButton);
    tbLay->addWidget(m_zoomInButton);
    tbLay->addWidget(m_fitButton);
    tbLay->addSpacing(12);
    tbLay->addWidget(m_pageLabel, 1);

    mainLay->addWidget(toolbar);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { background: #606060; }");

    m_pageCanvas = new QLabel(m_scrollArea);
    m_pageCanvas->setAlignment(Qt::AlignCenter);
    m_pageCanvas->setText("No document loaded");
    m_pageCanvas->setStyleSheet("color: #ddd; font-size: 14pt; "
                                "background: #606060;");
    m_scrollArea->setWidget(m_pageCanvas);
    m_scrollArea->setWidgetResizable(false);

    mainLay->addWidget(m_scrollArea, 1);

    connect(m_prevButton,    &QPushButton::clicked, this, &PdfPreview::onPreviousPage);
    connect(m_nextButton,    &QPushButton::clicked, this, &PdfPreview::onNextPage);
    connect(m_zoomInButton,  &QPushButton::clicked, this, &PdfPreview::onZoomIn);
    connect(m_zoomOutButton, &QPushButton::clicked, this, &PdfPreview::onZoomOut);
    connect(m_fitButton,     &QPushButton::clicked, this, &PdfPreview::onFitWindow);

    updateButtonStates();
}

bool PdfPreview::loadFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;
    if (!QFile::exists(filePath)) {
        m_pageCanvas->setText("File not found:\n" + filePath);
        m_totalPages = 0;
        m_currentPage = 0;
        updateButtonStates();
        return false;
    }

    const qint64 fileSize = QFileInfo(filePath).size();
    if (fileSize > MAX_FILE_SIZE_BYTES) {
        m_pageCanvas->setText("PDF too large for preview (>100 MB)");
        m_totalPages = 0;
        m_currentPage = 0;
        m_document.reset();
        updateButtonStates();
        return false;
    }

#ifdef DOCUSEARCH_HAS_POPPLER
    try {
        // poppler::document::load_from_file returns a raw pointer that
        // we own. Wrap it in shared_ptr<void> with a custom deleter.
        poppler::document* docRaw = poppler::document::load_from_file(
            filePath.toStdString());
        if (!docRaw || docRaw->pages() == 0) {
            delete docRaw;
            m_pageCanvas->setText("Cannot open PDF\n(locked or corrupted)");
            m_document.reset();
            m_totalPages = 0;
            updateButtonStates();
            return false;
        }

        const int realPages = docRaw->pages();
        m_totalPages = std::min(realPages, MAX_PAGES);

        m_document = std::shared_ptr<void>(
            docRaw,
            [](void* p) { delete static_cast<poppler::document*>(p); });

        m_currentPage = 0;
        m_zoomLevel   = 1.0;

        if (realPages > MAX_PAGES) {
            m_pageLabel->setText(QString("Page 1 of %1 (showing first %2 of %3 pages)")
                .arg(m_totalPages).arg(MAX_PAGES).arg(realPages));
        } else {
            m_pageLabel->setText(QString("Page 1 of %1").arg(m_totalPages));
        }

        renderPage(0);
        updateButtonStates();
        return true;
    } catch (const std::exception& e) {
        m_pageCanvas->setText(QString("Error opening PDF:\n%1").arg(e.what()));
        DS_WARN("Preview", QString("PdfPreview exception: %1").arg(e.what()));
        m_document.reset();
        m_totalPages = 0;
        updateButtonStates();
        return false;
    } catch (...) {
        m_pageCanvas->setText("Unknown error opening PDF.");
        m_document.reset();
        m_totalPages = 0;
        updateButtonStates();
        return false;
    }
#else
    m_pageCanvas->setText("PDF preview not available\n(Poppler not built into DocuSearch)");
    return false;
#endif
}

void PdfPreview::renderPage(int pageNumber) {
    if (pageNumber < 0 || pageNumber >= m_totalPages) return;
    if (!m_document) return;

#ifdef DOCUSEARCH_HAS_POPPLER
    try {
        auto* doc = static_cast<poppler::document*>(m_document.get());
        // create_page returns a raw poppler::page* that we must delete.
        poppler::page* page = doc->create_page(pageNumber);
        if (!page) {
            m_pageCanvas->setText("Failed to render page");
            return;
        }

        poppler::page_renderer renderer;
        renderer.set_render_hint(poppler::page_renderer::text_antialiasing);
        // Note: image_antialiasing is not available in all poppler versions.

        const double dpi = RENDER_DPI * m_zoomLevel;
        auto img_data = renderer.render_page(page, dpi, dpi);

        // page is a raw pointer — delete it immediately after rendering.
        delete page;
        page = nullptr;

        if (!img_data.is_valid()) {
            m_pageCanvas->setText("Failed to render page");
            return;
        }

        // poppler::image stores bytes as ARGB32 (or similar) — convert to QImage.
        char* dataPtr = const_cast<char*>(img_data.data());
        if (!dataPtr) {
            m_pageCanvas->setText("Failed to render page");
            return;
        }

        QImage qimg(reinterpret_cast<const uchar*>(dataPtr),
                    img_data.width(), img_data.height(),
                    img_data.bytes_per_row(),
                    QImage::Format_ARGB32);
        if (qimg.isNull()) {
            m_pageCanvas->setText("Failed to render page");
            return;
        }

        m_pageCanvas->setPixmap(QPixmap::fromImage(qimg.copy()));
        m_pageCanvas->setStyleSheet("");
        m_pageCanvas->resize(qimg.size());

        m_currentPage = pageNumber;
        m_pageLabel->setText(QString("Page %1 of %2")
            .arg(m_currentPage + 1).arg(m_totalPages));
        updateButtonStates();
    } catch (const std::exception& e) {
        m_pageCanvas->setText(QString("Failed to render page:\n%1").arg(e.what()));
        DS_WARN("Preview", QString("PdfPreview renderPage exception: %1").arg(e.what()));
    } catch (...) {
        m_pageCanvas->setText("Failed to render page");
    }
#else
    (void)pageNumber;
#endif
}

void PdfPreview::onNextPage() {
    if (m_currentPage < m_totalPages - 1) renderPage(m_currentPage + 1);
}

void PdfPreview::onPreviousPage() {
    if (m_currentPage > 0) renderPage(m_currentPage - 1);
}

void PdfPreview::onZoomIn() {
    m_zoomLevel *= 1.25;
    renderPage(m_currentPage);
}

void PdfPreview::onZoomOut() {
    m_zoomLevel = std::max(0.1, m_zoomLevel / 1.25);
    renderPage(m_currentPage);
}

void PdfPreview::onFitWindow() {
    m_zoomLevel = 1.0;
    renderPage(m_currentPage);
}

void PdfPreview::clear() {
    m_document.reset();
    m_pageCanvas->clear();
    m_pageCanvas->setText("No document loaded");
    m_pageCanvas->setStyleSheet("color: #ddd; font-size: 14pt; background: #606060;");
    m_currentPage = 0;
    m_totalPages  = 0;
    m_zoomLevel   = 1.0;
    m_pageLabel->setText("No document loaded");
    updateButtonStates();
}

void PdfPreview::updateButtonStates() {
    m_prevButton->setEnabled(m_currentPage > 0);
    m_nextButton->setEnabled(m_currentPage < m_totalPages - 1);
    m_zoomInButton->setEnabled(m_totalPages > 0);
    m_zoomOutButton->setEnabled(m_totalPages > 0);
    m_fitButton->setEnabled(m_totalPages > 0);
}

} // namespace DocuSearch
