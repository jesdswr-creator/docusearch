#pragma once

// ============================================================
// ImagePreview.h - Preview widget for image files
// ============================================================

#include "IFilePreview.h"

#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QImage>
#include <QPushButton>

namespace DocuSearch {

class ImagePreview : public QWidget, public IFilePreview {
    Q_OBJECT
public:
    explicit ImagePreview(QWidget* parent = nullptr);

    bool loadFile(const QString& filePath) override;
    QWidget* getWidget() override { return this; }
    void clear() override;
    QString getTypeName() const override { return "Image"; }

private slots:
    void onZoomIn();
    void onZoomOut();
    void onFitWindow();

private:
    void displayImage();

    QLabel*        m_imageLabel  = nullptr;
    QScrollArea*   m_scrollArea  = nullptr;
    QLabel*        m_infoLabel   = nullptr;
    QImage         m_originalImage;
    double         m_zoomLevel   = 1.0;

    static constexpr qint64 MAX_IMAGE_SIZE_BYTES = 50 * 1024 * 1024;  // 50 MB
};

} // namespace DocuSearch
