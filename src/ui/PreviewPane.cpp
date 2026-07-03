// ============================================================
// PreviewPane.cpp - Document viewer matching reference design
// ============================================================

#include "PreviewPane.h"
#include "IconUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QPalette>
#include <QClipboard>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QSaveFile>
#include <QMessageBox>

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
    hLay->setContentsMargins(16, 10, 16, 10);
    hLay->setSpacing(8);

    viewerTitle_ = new QLabel(header);
    viewerTitle_->setObjectName("viewerTitle");
    viewerTitle_->setText("No file selected");
    hLay->addWidget(viewerTitle_);

    // Page navigation: ‹ [1] /2 ›
    prevPageBtn_ = new QPushButton(header);
    prevPageBtn_->setObjectName("pageBtn");
    prevPageBtn_->setCursor(Qt::PointingHandCursor);
    prevPageBtn_->setToolTip("Previous page");
    prevPageBtn_->setText("‹");
    pageInput_ = new QLineEdit(header);
    pageInput_->setObjectName("pageInput");
    pageInput_->setText("1");
    pageTotal_ = new QLabel(header);
    pageTotal_->setObjectName("pageTotal");
    pageTotal_->setText("/ 1");
    nextPageBtn_ = new QPushButton(header);
    nextPageBtn_->setObjectName("pageBtn");
    nextPageBtn_->setCursor(Qt::PointingHandCursor);
    nextPageBtn_->setToolTip("Next page");
    nextPageBtn_->setText("›");
    hLay->addWidget(prevPageBtn_);
    hLay->addWidget(pageInput_);
    hLay->addWidget(pageTotal_);
    hLay->addWidget(nextPageBtn_);

    // Zoom controls: − 100% +
    zoomOutBtn_ = new QPushButton(header);
    zoomOutBtn_->setObjectName("zoomBtn");
    zoomOutBtn_->setCursor(Qt::PointingHandCursor);
    zoomOutBtn_->setToolTip("Zoom out");
    zoomOutBtn_->setText("−");
    zoomLevel_ = new QLabel(header);
    zoomLevel_->setObjectName("zoomLevel");
    zoomLevel_->setText("100%");
    zoomInBtn_ = new QPushButton(header);
    zoomInBtn_->setObjectName("zoomBtn");
    zoomInBtn_->setCursor(Qt::PointingHandCursor);
    zoomInBtn_->setToolTip("Zoom in");
    zoomInBtn_->setText("+");
    hLay->addSpacing(8);
    hLay->addWidget(zoomOutBtn_);
    hLay->addWidget(zoomLevel_);
    hLay->addWidget(zoomInBtn_);

    hLay->addStretch();

    // Action buttons: Fit, Rotate, More
    fitBtn_ = new QPushButton(header);
    fitBtn_->setObjectName("iconBtn");
    fitBtn_->setCursor(Qt::PointingHandCursor);
    fitBtn_->setToolTip("Fit to page");
    rotateBtn_ = new QPushButton(header);
    rotateBtn_->setObjectName("iconBtn");
    rotateBtn_->setCursor(Qt::PointingHandCursor);
    rotateBtn_->setToolTip("Rotate");
    moreBtn_ = new QPushButton(header);
    moreBtn_->setObjectName("iconBtn");
    moreBtn_->setCursor(Qt::PointingHandCursor);
    moreBtn_->setToolTip("More");
    hLay->addWidget(fitBtn_);
    hLay->addWidget(rotateBtn_);
    hLay->addWidget(moreBtn_);

    v->addWidget(header);

    // ---- Viewer body (document page text) ----
    documentPage_ = new QTextEdit(this);
    documentPage_->setObjectName("documentPage");
    documentPage_->setReadOnly(true);
    documentPage_->setPlaceholderText("Select a file to view its content.");
    v->addWidget(documentPage_, 1);

    // ---- Extracted panel (bottom) ----
    auto* extractedPanel = new QWidget(this);
    extractedPanel->setObjectName("extractedPanel");
    auto* epLay = new QVBoxLayout(extractedPanel);
    epLay->setContentsMargins(0, 0, 0, 0);
    epLay->setSpacing(0);

    // Tabs row
    auto* tabsRow = new QWidget(extractedPanel);
    auto* tabsLay = new QHBoxLayout(tabsRow);
    tabsLay->setContentsMargins(16, 0, 16, 0);
    tabsLay->setSpacing(0);
    tabGroup_ = new QButtonGroup(this);
    tabGroup_->setExclusive(true);

    tabExtracted_ = new QPushButton("Extracted Text", tabsRow);
    tabExtracted_->setObjectName("extractedTab");
    tabExtracted_->setCursor(Qt::PointingHandCursor);
    tabExtracted_->setCheckable(true);
    tabExtracted_->setChecked(true);

    tabSummary_ = new QPushButton("AI Summary", tabsRow);
    tabSummary_->setObjectName("extractedTab");
    tabSummary_->setCursor(Qt::PointingHandCursor);
    tabSummary_->setCheckable(true);

    tabHighlights_ = new QPushButton("Highlights", tabsRow);
    tabHighlights_->setObjectName("extractedTab");
    tabHighlights_->setCursor(Qt::PointingHandCursor);
    tabHighlights_->setCheckable(true);

    tabRelated_ = new QPushButton("Related", tabsRow);
    tabRelated_->setObjectName("extractedTab");
    tabRelated_->setCursor(Qt::PointingHandCursor);
    tabRelated_->setCheckable(true);

    tabGroup_->addButton(tabExtracted_, 0);
    tabGroup_->addButton(tabSummary_, 1);
    tabGroup_->addButton(tabHighlights_, 2);
    tabGroup_->addButton(tabRelated_, 3);

    tabsLay->addWidget(tabExtracted_);
    tabsLay->addWidget(tabSummary_);
    tabsLay->addWidget(tabHighlights_);
    tabsLay->addWidget(tabRelated_);
    tabsLay->addStretch();
    epLay->addWidget(tabsRow);

    // Content
    extractedContent_ = new QTextEdit(extractedPanel);
    extractedContent_->setObjectName("extractedContent");
    extractedContent_->setReadOnly(true);
    extractedContent_->setPlaceholderText("Extracted text will appear here after content extraction.");
    extractedContent_->setMaximumHeight(150);
    extractedContent_->setMinimumHeight(80);
    epLay->addWidget(extractedContent_);

    // Action buttons row: spacer + Copy + Download
    auto* actionsRow = new QWidget(extractedPanel);
    auto* actLay = new QHBoxLayout(actionsRow);
    actLay->setContentsMargins(16, 0, 16, 8);
    actLay->setSpacing(4);
    actLay->addStretch();
    copyBtn_ = new QPushButton(actionsRow);
    copyBtn_->setObjectName("extractedActionBtn");
    copyBtn_->setCursor(Qt::PointingHandCursor);
    copyBtn_->setToolTip("Copy extracted text");
    downloadBtn_ = new QPushButton(actionsRow);
    downloadBtn_->setObjectName("extractedActionBtn");
    downloadBtn_->setCursor(Qt::PointingHandCursor);
    downloadBtn_->setToolTip("Download extracted text");
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

    refreshIcons();
}

void PreviewPane::setThumbnail(const QPixmap& pix) {
    Q_UNUSED(pix);
    // Thumbnails are not shown in this version — the document text
    // is shown directly in the viewer body.
}

void PreviewPane::setExtractedText(const QString& text) {
    currentExtracted_ = text;
    extractedContent_->setPlainText(text);
}

void PreviewPane::setFilePath(const QString& path) {
    currentPath_ = path;
    if (path.isEmpty()) {
        viewerTitle_->setText("No file selected");
    } else {
        QFileInfo fi(path);
        viewerTitle_->setText(fi.fileName());
    }
}

void PreviewPane::setPageInfo(int currentPage, int totalPages) {
    currentPage_ = qMax(1, currentPage);
    totalPages_  = qMax(1, totalPages);
    if (currentPage_ > totalPages_) currentPage_ = totalPages_;
    updatePageDisplay();
}

void PreviewPane::setDocumentText(const QString& text) {
    documentPage_->setPlainText(text);
}

void PreviewPane::clear() {
    documentPage_->clear();
    extractedContent_->clear();
    viewerTitle_->setText("No file selected");
    currentPath_.clear();
    currentExtracted_.clear();
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
    }
}

void PreviewPane::onNextPage() {
    if (currentPage_ < totalPages_) {
        ++currentPage_;
        updatePageDisplay();
    }
}

void PreviewPane::onZoomIn() {
    zoomPercent_ = qMin(300, zoomPercent_ + 10);
    updateZoomDisplay();
}

void PreviewPane::onZoomOut() {
    zoomPercent_ = qMax(25, zoomPercent_ - 10);
    updateZoomDisplay();
}

void PreviewPane::onCopyExtracted() {
    if (currentExtracted_.isEmpty()) return;
    QApplication::clipboard()->setText(currentExtracted_);
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
    }
}

void PreviewPane::onTabClicked(int id) {
    // All tabs show the extracted text for now (AI Summary / Highlights
    // / Related require additional backends we don't have). The active
    // tab is visually indicated by the QSS :checked state.
    if (id == 0) {
        extractedContent_->setPlainText(currentExtracted_);
    } else if (id == 1) {
        extractedContent_->setPlainText(
            currentExtracted_.isEmpty()
                ? "(AI summary not available — needs an LLM backend.)"
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

void PreviewPane::updatePageDisplay() {
    pageInput_->setText(QString::number(currentPage_));
    pageTotal_->setText(QString("/ %1").arg(totalPages_));
    prevPageBtn_->setEnabled(currentPage_ > 1);
    nextPageBtn_->setEnabled(currentPage_ < totalPages_);
}

void PreviewPane::updateZoomDisplay() {
    zoomLevel_->setText(QString::number(zoomPercent_) + "%");
    // Apply zoom to the document page text via font scaling.
    QFont f = documentPage_->font();
    f.setPointSize(qMax(8, int(13 * zoomPercent_ / 100)));
    documentPage_->setFont(f);
}

void PreviewPane::refreshIcons() {
    QColor textColor = qApp->palette().color(QPalette::Text);

    fitBtn_->setIcon(loadLucideIcon("maximize-2", textColor, 14));
    fitBtn_->setIconSize(QSize(14, 14));

    rotateBtn_->setIcon(loadLucideIcon("rotate-cw", textColor, 14));
    rotateBtn_->setIconSize(QSize(14, 14));

    moreBtn_->setIcon(loadLucideIcon("more-horizontal", textColor, 14));
    moreBtn_->setIconSize(QSize(14, 14));

    copyBtn_->setIcon(loadLucideIcon("copy", textColor, 12));
    copyBtn_->setIconSize(QSize(12, 12));

    downloadBtn_->setIcon(loadLucideIcon("download", textColor, 12));
    downloadBtn_->setIconSize(QSize(12, 12));
}

} // namespace DocuSearch
