#pragma once

#include <QWidget>
#include <QLabel>
#include <QTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace DocuSearch {

class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(QWidget* parent = nullptr);

    void setThumbnail(const QPixmap& pix);
    void setExtractedText(const QString& text);
    void setFilePath(const QString& path);
    void clear();

signals:
    void openRequested(const QString& path);
    void ocrRequested(const QString& path);

private slots:
    void onOpenClicked();
    void onOcrClicked();

private:
    QLabel*     thumbLabel_;
    QTextEdit*  textEdit_;
    QLabel*     pathLabel_;
    QPushButton* openBtn_;
    QPushButton* ocrBtn_;
};

} // namespace DocuSearch
