#pragma once

#include <QObject>
#include <QString>
#include <QImage>

namespace DocuSearch {

// Generates thumbnails / first-page previews for documents.
// Uses Qt's built-in image loading for raster formats; PDFs require Poppler
// (linked at build time). Results are cached on disk.
class ThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailGenerator(const QString& cacheDir, QObject* parent = nullptr);

    // Returns a thumbnail for `path`. May be empty if generation failed.
    // Synchronous — call from worker thread.
    QImage thumbnail(const QString& path, int maxSize);

    // Clear cached thumbnails.
    void clearCache();

private:
    QString cacheDir_;
    QString cacheKey(const QString& path, qint64 mtime, int size) const;
};

} // namespace DocuSearch
