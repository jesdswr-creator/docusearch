// ============================================================
// ImagePreview.cpp - Image file preview with zoom controls
// ============================================================

#include "ImagePreview.h"

#include <QFile>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>

namespace DocuSearch {

ImagePreview::ImagePreview(QWidget* parent)
    : QWidget(parent) {
    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // Toolbar with zoom buttons
    auto* toolbar = new QWidget(this);
    auto* tbLay = new QHBoxLayout(toolbar);
    tbLay->setContentsMargins(4, 4, 4, 4);
    tbLay->setSpacing(4);

    auto* zoomInBtn = new QPushButton("+", toolbar);
    zoomInBtn->setToolTip("Zoom In");
    zoomInBtn->setFixedWidth(32);
    auto* zoomOutBtn = new QPushButton("-", toolbar);
    zoomOutBtn->setToolTip("Zoom Out");
    zoomOutBtn->setFixedWidth(32);
    auto* fitBtn = new QPushButton("Fit", toolbar);
    fitBtn->setToolTip("Fit to Window");

    tbLay->addWidget(zoomInBtn);
    tbLay->addWidget(zoomOutBtn);
    tbLay->addWidget(fitBtn);
    tbLay->addStretch();
    mainLay->addWidget(toolbar);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet(
        "color: #666; font-size: 10pt; padding: 4px 8px; "
        "background: #f5f5f5; border-bottom: 1px solid #ddd;");
    mainLay->addWidget(m_infoLabel);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setBackgroundRole(QPalette::Light);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setStyleSheet("QScrollArea { background: #f0f0f0; }");

    m_imageLabel = new QLabel(m_scrollArea);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setText("No image loaded");
    m_imageLabel->setStyleSheet("color: #999; font-size: 14pt;");
    m_scrollArea->setWidget(m_imageLabel);

    mainLay->addWidget(m_scrollArea, 1);

    connect(zoomInBtn,  &QPushButton::clicked, this, &ImagePreview::onZoomIn);
    connect(zoomOutBtn, &QPushButton::clicked, this, &ImagePreview::onZoomOut);
    connect(fitBtn,     &QPushButton::clicked, this, &ImagePreview::onFitWindow);
}

bool ImagePreview::loadFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;
    if (!QFile::exists(filePath)) {
        m_imageLabel->setText("File not found:\n" + filePath);
        m_infoLabel->setText("");
        return false;
    }

    const qint64 fileSize = QFileInfo(filePath).size();
    if (fileSize > MAX_IMAGE_SIZE_BYTES) {
        m_imageLabel->setText("Image too large for preview (>50 MB)");
        m_infoLabel->setText("");
        m_originalImage = QImage();
        return false;
    }

    try {
        if (!m_originalImage.load(filePath)) {
            m_imageLabel->setText("Cannot load image\n(unsupported or corrupted format)");
            m_infoLabel->setText("");
            return false;
        }
        if (m_originalImage.isNull()) {
            m_imageLabel->setText("Cannot load image\n(unsupported or corrupted format)");
            return false;
        }
        m_zoomLevel = 1.0;
        m_infoLabel->setText(QString("%1  |  %2 x %3 px  |  %4 KB")
            .arg(QFileInfo(filePath).fileName())
            .arg(m_originalImage.width())
            .arg(m_originalImage.height())
            .arg(fileSize / 1024.0, 0, 'f', 1));
        displayImage();
        return true;
    } catch (const std::exception& e) {
        m_imageLabel->setText(QString("Error loading image: %1").arg(e.what()));
        return false;
    } catch (...) {
        m_imageLabel->setText("Unknown error loading image.");
        return false;
    }
}

void ImagePreview::displayImage() {
    if (m_originalImage.isNull()) return;

    QSize newSize = m_originalImage.size() * m_zoomLevel;
    if (newSize.width() < 1) newSize.setWidth(1);
    if (newSize.height() < 1) newSize.setHeight(1);

    QImage scaled = m_originalImage.scaled(
        newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(QPixmap::fromImage(scaled));
    m_imageLabel->resize(scaled.size());
    m_imageLabel->setStyleSheet("");
}

void ImagePreview::onZoomIn() {
    m_zoomLevel *= 1.25;
    displayImage();
}

void ImagePreview::onZoomOut() {
    m_zoomLevel = std::max(0.1, m_zoomLevel / 1.25);
    displayImage();
}

void ImagePreview::onFitWindow() {
    if (m_originalImage.isNull()) return;
    const int w = m_scrollArea->viewport()->width();
    if (w <= 0 || m_originalImage.width() <= 0) return;
    m_zoomLevel = (double)w / std::max(1, m_originalImage.width());
    displayImage();
}

void ImagePreview::clear() {
    m_originalImage = QImage();
    m_imageLabel->clear();
    m_imageLabel->setText("No image loaded");
    m_imageLabel->setStyleSheet("color: #999; font-size: 14pt;");
    m_infoLabel->setText("");
    m_zoomLevel = 1.0;
}

} // namespace DocuSearch
