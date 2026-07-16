// ============================================================
// PreviewPane.cpp - Document viewer with rich preview
// ============================================================

#include "PreviewPane.h"
#include "IconUtils.h"
#include "../core/Constants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QPalette>
#include <QClipboard>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QScrollArea>
#include <QImage>
#include <QPixmap>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QPrintDialog>
#include <QPrinter>
#include <QTimer>
#include <QRegularExpression>
#include <QTransform>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>

#ifdef DOCUSEARCH_HAS_POPPLER
#  include <poppler-document.h>
#  include <poppler-page.h>
#  include <poppler-page-renderer.h>
#endif

namespace DocuSearch {

PreviewPane::PreviewPane(QWidget* parent) : QWidget(parent) {
    setObjectName("viewerPanel");

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // ---- Viewer header ----
    auto* header = new QWidget(this);
    header->setObjectName("viewerHeader");
    auto* hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(12, 8, 12, 8);
    hLay->setSpacing(6);

    // File type icon + filename
    fileIconLbl_ = new QLabel(header);
    fileIconLbl_->setFixedSize(20, 20);
    hLay->addWidget(fileIconLbl_);

    viewerTitle_ = new QLabel(header);
    viewerTitle_->setObjectName("viewerTitle");
    viewerTitle_->setText("No file selected");
    hLay->addWidget(viewerTitle_);

    // No page navigation buttons — PDF pages are shown in a scroll area

    // Zoom controls: − 100% +
    zoomOutBtn_ = new QPushButton(header);
    zoomOutBtn_->setObjectName("zoomBtn");
    zoomOutBtn_->setCursor(Qt::PointingHandCursor);
    zoomOutBtn_->setToolTip("Zoom out");
    zoomOutBtn_->setText("−");
    zoomOutBtn_->setFixedSize(32, 32);
    zoomLevel_ = new QLabel(header);
    zoomLevel_->setObjectName("zoomLevel");
    zoomLevel_->setText("100%");
    zoomInBtn_ = new QPushButton(header);
    zoomInBtn_->setObjectName("zoomBtn");
    zoomInBtn_->setCursor(Qt::PointingHandCursor);
    zoomInBtn_->setToolTip("Zoom in");
    zoomInBtn_->setText("+");
    zoomInBtn_->setFixedSize(32, 32);
    hLay->addSpacing(8);
    hLay->addWidget(zoomOutBtn_);
    hLay->addWidget(zoomLevel_);
    hLay->addWidget(zoomInBtn_);

    hLay->addStretch();

    // Action buttons: Open (blue), OCR (green) — only working buttons
    openBtn_ = new QPushButton(header);
    openBtn_->setObjectName("openBtn");
    openBtn_->setCursor(Qt::PointingHandCursor);
    openBtn_->setToolTip("Open in default application");
    openBtn_->setText("Open");

    ocrBtn_ = new QPushButton(header);
    ocrBtn_->setObjectName("ocrBtn");
    ocrBtn_->setCursor(Qt::PointingHandCursor);
    ocrBtn_->setToolTip("Run OCR on this file (for scanned PDFs and images)");
    ocrBtn_->setText("OCR");

    // More button — created but hidden (Print/Export not essential)
    moreBtn_ = new QPushButton(header);
    moreBtn_->setObjectName("moreActionBtn");
    moreBtn_->setVisible(false);
    hLay->addWidget(openBtn_);
    hLay->addWidget(ocrBtn_);

    v->addWidget(header);

    // ---- Viewer body ----
    // We use a QScrollArea containing a stacked widget that can show
    // either a page image (for PDF/image preview) or a text browser
    // (for DOCX/XLSX/PPTX/TXT preview).
    previewScroll_ = new QScrollArea(this);
    previewScroll_->setWidgetResizable(true);
    previewScroll_->setFrameShape(QFrame::NoFrame);
    previewScroll_->setAlignment(Qt::AlignCenter);

    // Page image label (for PDF/image preview)
    pageImageLbl_ = new QLabel(previewScroll_);
    pageImageLbl_->setAlignment(Qt::AlignCenter);
    pageImageLbl_->setText("Select a file to preview");
    pageImageLbl_->setMinimumSize(400, 500);

    // Text browser (for text-based preview)
    documentPage_ = new QTextBrowser(previewScroll_);
    documentPage_->setObjectName("documentPage");
    documentPage_->setOpenExternalLinks(true);
    documentPage_->setPlaceholderText("Select a file to view its content.");

    // Default to text mode
    previewScroll_->setWidget(documentPage_);
    v->addWidget(previewScroll_, 1);

    // ---- Extracted panel (bottom) ----
    auto* extractedPanel = new QWidget(this);
    extractedPanel->setObjectName("extractedPanel");
    auto* epLay = new QVBoxLayout(extractedPanel);
    epLay->setContentsMargins(0, 8, 0, 0);
    epLay->setSpacing(4);

    // Tabs row — only "Extracted Text" tab
    auto* tabsRow = new QWidget(extractedPanel);
    auto* tabsLay = new QHBoxLayout(tabsRow);
    tabsLay->setContentsMargins(16, 4, 16, 4);
    tabsLay->setSpacing(0);
    tabGroup_ = new QButtonGroup(this);
    tabGroup_->setExclusive(true);

    tabExtracted_ = new QPushButton("Extracted Text", tabsRow);
    tabExtracted_->setObjectName("extractedTab");
    tabExtracted_->setCursor(Qt::PointingHandCursor);
    tabExtracted_->setCheckable(true);
    tabExtracted_->setChecked(true);

    tabSummary_ = new QPushButton(this); tabSummary_->setVisible(false);
    tabHighlights_ = new QPushButton(this); tabHighlights_->setVisible(false);
    tabRelated_ = new QPushButton(this); tabRelated_->setVisible(false);

    tabGroup_->addButton(tabExtracted_, 0);
    tabGroup_->addButton(tabSummary_, 1);
    tabGroup_->addButton(tabHighlights_, 2);
    tabGroup_->addButton(tabRelated_, 3);

    tabsLay->addWidget(tabExtracted_);
    tabsLay->addStretch();
    epLay->addWidget(tabsRow);

    // Content
    extractedContent_ = new QTextEdit(extractedPanel);
    extractedContent_->setObjectName("extractedContent");
    extractedContent_->setReadOnly(true);
    extractedContent_->setPlaceholderText("Extracted text will appear here after content extraction.");
    extractedContent_->setMaximumHeight(140);
    extractedContent_->setMinimumHeight(80);
    epLay->addWidget(extractedContent_);

    // Action buttons row: spacer + Copy + Download
    auto* actionsRow = new QWidget(extractedPanel);
    auto* actLay = new QHBoxLayout(actionsRow);
    actLay->setContentsMargins(16, 0, 16, 8);
    actLay->setSpacing(4);
    actLay->addStretch();
    copyBtn_ = new QPushButton("Copy", actionsRow);
    copyBtn_->setObjectName("copyBtn");
    copyBtn_->setCursor(Qt::PointingHandCursor);
    copyBtn_->setToolTip("Copy extracted text to clipboard");
    downloadBtn_ = new QPushButton("Save", actionsRow);
    downloadBtn_->setObjectName("downloadBtn");
    downloadBtn_->setCursor(Qt::PointingHandCursor);
    downloadBtn_->setToolTip("Save extracted text to file");
    actLay->addWidget(copyBtn_);
    actLay->addWidget(downloadBtn_);
    epLay->addWidget(actionsRow);

    v->addWidget(extractedPanel);

    // ---- Connections ----
    connect(prevPageBtn_, &QPushButton::clicked, this, &PreviewPane::onPrevPage);
    connect(nextPageBtn_, &QPushButton::clicked, this, &PreviewPane::onNextPage);
    connect(zoomInBtn_,   &QPushButton::clicked, this, &PreviewPane::onZoomIn);
    connect(zoomOutBtn_,  &QPushButton::clicked, this, &PreviewPane::onZoomOut);
    connect(copyBtn_,     &QPushButton::clicked, this, &PreviewPane::onCopyExtracted);
    connect(downloadBtn_, &QPushButton::clicked, this, &PreviewPane::onDownloadExtracted);
    connect(tabGroup_,    &QButtonGroup::idClicked, this, &PreviewPane::onTabClicked);
    connect(openBtn_,     &QPushButton::clicked, this, &PreviewPane::onOpenClicked);
    connect(ocrBtn_,      &QPushButton::clicked, this, &PreviewPane::onOcrClicked);
    connect(moreBtn_,     &QPushButton::clicked, this, &PreviewPane::onMoreClicked);
    connect(pageInput_,   &QLineEdit::returnPressed, this, &PreviewPane::onPageInputChanged);

    refreshIcons();
}

void PreviewPane::setThumbnail(const QPixmap& pix) {
    Q_UNUSED(pix);
}

void PreviewPane::setExtractedText(const QString& text) {
    currentExtracted_ = text;
    if (tabExtracted_->isChecked()) {
        extractedContent_->setPlainText(text);
    }
}

void PreviewPane::setFilePath(const QString& path) {
    currentPath_ = path;
    if (path.isEmpty()) {
        viewerTitle_->setText("No file selected");
        currentExt_.clear();
        documentPage_->setHtml(
            "<div style='color: #999; text-align: center; padding: 60px;'>"
            "<p style='font-size: 15px;'>Select a file to preview</p></div>");
        return;
    }
    QFileInfo fi(path);
    viewerTitle_->setText(fi.fileName());
    currentExt_ = fi.suffix().toLower();

    // For ALL file types, show the extracted text in the text browser.
    // This is crash-safe — no image rendering, no widget swapping.
    // PDF page images are intentionally NOT rendered (caused crashes).
    showTextPreview(currentDocumentText_);
}

void PreviewPane::setPageInfo(int currentPage, int totalPages) {
    currentPage_ = qMax(1, currentPage);
    totalPages_  = qMax(1, totalPages);
    if (currentPage_ > totalPages_) currentPage_ = totalPages_;
    updatePageDisplay();
}

void PreviewPane::setDocumentText(const QString& text) {
    currentDocumentText_ = text;
    // If we're in text preview mode, update the display.
    if (!currentPath_.isEmpty()) {
        QFileInfo fi(currentPath_);
        const QString ext = fi.suffix().toLower();
        if (ext != "pdf" && ext != "png" && ext != "jpg" &&
            ext != "jpeg" && ext != "bmp" && ext != "gif" &&
            ext != "webp") {
            showTextPreview(text);
        }
    }
}

void PreviewPane::setSearchQuery(const QString& query) {
    searchQuery_ = query.trimmed();
    if (!currentPath_.isEmpty() && !searchQuery_.isEmpty()) {
        highlightSearchTerms();
    }
}

void PreviewPane::clear() {
    pageImageLbl_->clear();
    pageImageLbl_->setText("Select a file to preview");
    documentPage_->clear();
    extractedContent_->clear();
    viewerTitle_->setText("No file selected");
    currentPath_.clear();
    currentExt_.clear();
    currentExtracted_.clear();
    currentDocumentText_.clear();
    currentPage_ = 1;
    totalPages_ = 1;
    updatePageDisplay();
}

void PreviewPane::onOpenClicked() {
    if (!currentPath_.isEmpty()) emit openRequested(currentPath_);
}

void PreviewPane::onOcrClicked() {
    if (!currentPath_.isEmpty()) emit ocrRequested(currentPath_);
}

void PreviewPane::onPrevPage() {
    if (currentPage_ > 1) {
        --currentPage_;
        updatePageDisplay();
        renderCurrentPage();
    }
}

void PreviewPane::onNextPage() {
    if (currentPage_ < totalPages_) {
        ++currentPage_;
        updatePageDisplay();
        renderCurrentPage();
    }
}

void PreviewPane::onZoomIn() {
    zoomPercent_ = qMin(400, zoomPercent_ + 25);
    updateZoomDisplay();
    renderCurrentPage();
}

void PreviewPane::onZoomOut() {
    zoomPercent_ = qMax(25, zoomPercent_ - 25);
    updateZoomDisplay();
    renderCurrentPage();
}

void PreviewPane::onCopyExtracted() {
    if (currentExtracted_.isEmpty()) return;
    QApplication::clipboard()->setText(currentExtracted_);
    copyBtn_->setText("Copied!");
    QTimer::singleShot(1500, this, [this]() { copyBtn_->setText("Copy"); });
}

void PreviewPane::onDownloadExtracted() {
    if (currentExtracted_.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Extracted Text",
        QFileInfo(currentPath_).completeBaseName() + ".txt",
        "Text files (*.txt);;All files (*.*)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream s(&f);
        s.setEncoding(QStringConverter::Utf8);
        s << currentExtracted_;
        f.close();
        downloadBtn_->setText("Saved!");
        QTimer::singleShot(1500, this, [this]() { downloadBtn_->setText("Save"); });
    }
}

void PreviewPane::onTabClicked(int id) {
    if (id == 0) {
        extractedContent_->setPlainText(currentExtracted_);
    } else if (id == 1) {
        extractedContent_->setPlainText(
            currentExtracted_.isEmpty()
                ? "(AI summary not available - needs an LLM backend.)"
                : currentExtracted_.left(500) + "...");
    } else if (id == 2) {
        extractedContent_->setPlainText(
            currentExtracted_.isEmpty()
                ? "(No highlights yet.)"
                : currentExtracted_.left(500) + "...");
    } else if (id == 3) {
        extractedContent_->setPlainText(
            "(Related documents will be shown here after indexing.)");
    }
}

void PreviewPane::onFitClicked() {
    // Reset zoom to 100%
    zoomPercent_ = 100;
    updateZoomDisplay();
    renderCurrentPage();
}

void PreviewPane::onRotateClicked() {
    rotation_ = (rotation_ + 90) % 360;
    renderCurrentPage();
}

void PreviewPane::onMoreClicked() {
    auto* menu = new QMenu(this);
    auto* printAction = new QAction("Print...", this);
    auto* exportPdfAction = new QAction("Export as PDF...", this);
    auto* copyPathAction = new QAction("Copy file path", this);
    menu->addAction(printAction);
    menu->addAction(exportPdfAction);
    menu->addSeparator();
    menu->addAction(copyPathAction);

    connect(printAction, &QAction::triggered, this, [this]{
        if (currentPath_.isEmpty()) return;
        QPrinter printer;
        QPrintDialog dlg(&printer, this);
        if (dlg.exec() == QDialog::Accepted) {
            documentPage_->print(&printer);
        }
    });
    connect(exportPdfAction, &QAction::triggered, this, [this]{
        if (currentPath_.isEmpty()) return;
        const QString out = QFileDialog::getSaveFileName(
            this, "Export as PDF",
            QFileInfo(currentPath_).completeBaseName() + ".pdf",
            "PDF (*.pdf)");
        if (out.isEmpty()) return;
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(out);
        documentPage_->print(&printer);
        QMessageBox::information(this, "Export", "Exported to " + out);
    });
    connect(copyPathAction, &QAction::triggered, this, [this]{
        if (!currentPath_.isEmpty())
            QApplication::clipboard()->setText(currentPath_);
    });

    menu->exec(moreBtn_->mapToGlobal(QPoint(0, moreBtn_->height())));
    menu->deleteLater();
}

void PreviewPane::onPageInputChanged() {
    bool ok = false;
    const int p = pageInput_->text().toInt(&ok);
    if (ok && p >= 1 && p <= totalPages_) {
        currentPage_ = p;
        updatePageDisplay();
        renderCurrentPage();
    } else {
        updatePageDisplay();  // reset to current page
    }
}

void PreviewPane::updatePageDisplay() {
    pageInput_->setText(QString::number(currentPage_));
    pageTotal_->setText(QString("/ %1").arg(totalPages_));
    prevPageBtn_->setEnabled(currentPage_ > 1);
    nextPageBtn_->setEnabled(currentPage_ < totalPages_);
    // Show/hide page navigation for non-paged documents.
    const bool isPaged = (currentExt_ == "pdf");
    prevPageBtn_->setVisible(isPaged);
    pageInput_->setVisible(isPaged);
    pageTotal_->setVisible(isPaged);
    nextPageBtn_->setVisible(isPaged);
}

void PreviewPane::updateZoomDisplay() {
    zoomLevel_->setText(QString::number(zoomPercent_) + "%");
}

void PreviewPane::renderCurrentPage() {
    if (currentExt_ == "pdf") {
        showPdfPreview();
    } else if (currentExt_ == "png" || currentExt_ == "jpg" ||
               currentExt_ == "jpeg" || currentExt_ == "bmp" ||
               currentExt_ == "gif" || currentExt_ == "webp") {
        showImagePreview(currentPath_);
    }
}

void PreviewPane::showPdfPreview() {
#ifdef DOCUSEARCH_HAS_POPPLER
    try {
        auto doc = poppler::document::load_from_file(currentPath_.toStdString());
        if (!doc || doc->pages() == 0) {
            setPreviewMode(false);
            documentPage_->setPlainText("Failed to open PDF for preview.");
            return;
        }
        totalPages_ = doc->pages();
        if (totalPages_ > Constants::kMaxPdfPreviewPages) {
            totalPages_ = Constants::kMaxPdfPreviewPages;
        }
        currentPage_ = 1;
        updatePageDisplay();

        // Render ALL pages and stack them vertically in the scroll area.
        // The user scrolls to see all pages instead of clicking next/prev.
        poppler::page_renderer renderer;
        renderer.set_render_hint(poppler::page_renderer::text_antialiasing);
        renderer.set_render_hint(poppler::page_renderer::antialiasing);

        const int baseDpi = Constants::kPdfPreviewDpi;
        const int dpi = qMax(36, int(baseDpi * zoomPercent_ / 100));

        // Create a container widget with vertical layout for all pages.
        auto* pagesContainer = new QWidget();
        auto* pagesLay = new QVBoxLayout(pagesContainer);
        pagesLay->setContentsMargins(20, 20, 20, 20);
        pagesLay->setSpacing(16);
        pagesLay->setAlignment(Qt::AlignCenter);

        for (int i = 0; i < totalPages_; ++i) {
            try {
                auto* page = doc->create_page(i);
                if (!page) continue;
                auto img_data = renderer.render_page(page, dpi, dpi);
                if (!img_data.is_valid()) continue;
                char* dataPtr = const_cast<char*>(img_data.data());
                if (!dataPtr) continue;
                QImage qimg(reinterpret_cast<const uchar*>(dataPtr),
                            img_data.width(), img_data.height(),
                            img_data.bytes_per_row(),
                            QImage::Format_ARGB32);
                if (qimg.isNull()) continue;

                if (rotation_ == 90) qimg = qimg.transformed(QTransform().rotate(90));
                else if (rotation_ == 180) qimg = qimg.transformed(QTransform().rotate(180));
                else if (rotation_ == 270) qimg = qimg.transformed(QTransform().rotate(270));

                auto* pageLbl = new QLabel(pagesContainer);
                pageLbl->setPixmap(QPixmap::fromImage(qimg));
                pagesLay->addWidget(pageLbl);
            } catch (...) {
                // Skip this page
            }
        }

        setPreviewMode(true);
        // Replace the scroll area's widget with the pages container.
        previewScroll_->setWidget(pagesContainer);
        // Delete the old pageImageLbl_ — it's replaced by the container.
        if (pageImageLbl_) {
            pageImageLbl_->hide();
        }
    } catch (const std::exception& e) {
        setPreviewMode(false);
        documentPage_->setPlainText(QString("PDF preview error: %1").arg(e.what()));
    } catch (...) {
        setPreviewMode(false);
        documentPage_->setPlainText("PDF preview failed.");
    }
#else
    setPreviewMode(false);
    documentPage_->setPlainText(
        "PDF image preview requires Poppler (not linked in this build).\n\n"
        "Extracted text is shown in the panel below.");
#endif
}

void PreviewPane::showImagePreview(const QString& path) {
    QImage img(path);
    if (img.isNull()) {
        setPreviewMode(false);
        documentPage_->setPlainText("Failed to load image: " + path);
        return;
    }

    // Apply zoom.
    if (zoomPercent_ != 100) {
        const int w = img.width() * zoomPercent_ / 100;
        const int h = img.height() * zoomPercent_ / 100;
        img = img.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // Apply rotation.
    if (rotation_ == 90) {
        img = img.transformed(QTransform().rotate(90));
    } else if (rotation_ == 180) {
        img = img.transformed(QTransform().rotate(180));
    } else if (rotation_ == 270) {
        img = img.transformed(QTransform().rotate(270));
    }

    setPreviewMode(true);
    QPixmap pix = QPixmap::fromImage(img);
    pageImageLbl_->setPixmap(pix);
    pageImageLbl_->resize(pix.size());
    totalPages_ = 1;
    currentPage_ = 1;
    updatePageDisplay();
}

void PreviewPane::showTextPreview(const QString& text) {
    setPreviewMode(false);

    QColor textColor("#1a1a1a");
    QColor mutedColor("#999");

    if (text.isEmpty()) {
        documentPage_->setHtml(
            "<div style='color: #999; text-align: center; padding: 60px;'>"
            "<p style='font-size: 15px;'>No content extracted yet.</p>"
            "<p style='font-size: 12px; color: #bbb;'>Click the purple Extract button to extract text.</p>"
            "</div>");
        return;
    }

    // Format text with proper HTML for readability.
    // Sheet/slide separators → blue banner headings.
    QString html = text.toHtmlEscaped();
    html.replace(QRegularExpression("^(--- .+? ---)$", QRegularExpression::MultilineOption),
                 "<h3 style='background-color: #2563eb; color: #fff; "
                 "padding: 8px 14px; margin: 16px 0 8px 0; border-radius: 6px; "
                 "font-size: 13px; font-weight: 700;'>\\1</h3>");
    // Tab → pipe separator for spreadsheet cells
    html.replace("\t", " &nbsp;|&nbsp; ");
    // Newlines → <br>
    html.replace("\n", "<br>");
    documentPage_->setHtml(QString("<div style='font-family: Segoe UI, Arial, sans-serif; "
                           "font-size: 13px; line-height: 1.7; color: #333; "
                           "padding: 8px;'>%1</div>").arg(html));
}

void PreviewPane::setPreviewMode(bool) {
    // No-op — always use text mode (documentPage_).
    // Image mode was removed to prevent crashes from widget swapping.
}

void PreviewPane::refreshIcons() {
    QColor textColor = qApp->palette().color(QPalette::Text);
    QColor whiteText("#ffffff");

    // File type icon in header (generic file icon)
    fileIconLbl_->setPixmap(loadLucidePixmap("file-text", QColor("#2563eb"), 18, devicePixelRatio()));

    // More button icon
    moreBtn_->setIcon(loadLucideIcon("more-horizontal", textColor, 16));
    moreBtn_->setIconSize(QSize(16, 16));

    // Open button icon (white on blue)
    openBtn_->setIcon(loadLucideIcon("upload", whiteText, 14));
    openBtn_->setIconSize(QSize(14, 14));

    // OCR button icon (white on green)
    ocrBtn_->setIcon(loadLucideIcon("eye", whiteText, 14));
    ocrBtn_->setIconSize(QSize(14, 14));

    // Copy button icon
    copyBtn_->setIcon(loadLucideIcon("copy", whiteText, 12));
    copyBtn_->setIconSize(QSize(12, 12));

    // Download button icon
    downloadBtn_->setIcon(loadLucideIcon("download", whiteText, 12));
    downloadBtn_->setIconSize(QSize(12, 12));
}

void PreviewPane::highlightSearchTerms() {
    if (searchQuery_.isEmpty() || currentDocumentText_.isEmpty()) return;

    // Only highlight in text preview mode (not PDF image mode)
    QFileInfo fi(currentPath_);
    const QString ext = fi.suffix().toLower();
    if (ext == "pdf" || ext == "png" || ext == "jpg" ||
        ext == "jpeg" || ext == "bmp" || ext == "gif" ||
        ext == "webp") {
        return;
    }

    QColor highlightColor(255, 248, 120);  // light yellow
    // No flags = case-insensitive search (Qt default).
    QTextDocument::FindFlags flags;

    // Highlight in document preview
    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextCursor cursor(documentPage_->document());
    while (!cursor.isNull() && !cursor.atEnd()) {
        cursor = documentPage_->document()->find(searchQuery_, cursor, flags);
        if (cursor.isNull()) break;
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(highlightColor);
        sel.cursor = cursor;
        extraSelections.append(sel);
    }
    documentPage_->setExtraSelections(extraSelections);

    // Highlight in extracted content panel
    QList<QTextEdit::ExtraSelection> extraSelections2;
    QTextCursor cursor2(extractedContent_->document());
    while (!cursor2.isNull() && !cursor2.atEnd()) {
        cursor2 = extractedContent_->document()->find(searchQuery_, cursor2, flags);
        if (cursor2.isNull()) break;
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(highlightColor);
        sel.cursor = cursor2;
        extraSelections2.append(sel);
    }
    extractedContent_->setExtraSelections(extraSelections2);
}

} // namespace DocuSearch
