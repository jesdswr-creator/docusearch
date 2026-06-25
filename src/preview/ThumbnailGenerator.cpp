// ============================================================
// ThumbnailGenerator.cpp
// ============================================================

#include "ThumbnailGenerator.h"
#include "../core/Logger.h"
#include "../core/FileUtils.h"
#include "../core/Constants.h"

#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QImageReader>

#ifdef DOCUSEARCH_HAS_POPPLER
#  include <poppler-document.h>
#  include <poppler-page.h>
#  include <poppler-page-renderer.h>
#  include <type_traits>
#  include <memory>
#endif

namespace DocuSearch {

ThumbnailGenerator::ThumbnailGenerator(const QString& cacheDir, QObject* parent)
    : QObject(parent), cacheDir_(cacheDir) {
    QDir().mkpath(cacheDir_);
}

QString ThumbnailGenerator::cacheKey(const QString& path, qint64 mtime, int size) const {
    const QString raw = QString("%1|%2|%3").arg(path).arg(mtime).arg(size);
    return QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex();
}

QImage ThumbnailGenerator::thumbnail(const QString& path, int maxSize) {
    const QFileInfo fi(path);
    if (!fi.exists()) return {};
    const QString key = cacheKey(path, fi.lastModified().toSecsSinceEpoch(), maxSize);
    const QString cacheFile = cacheDir_ + "/" + key + ".png";

    if (QFileInfo::exists(cacheFile)) {
        QImage img(cacheFile);
        if (!img.isNull()) return img;
    }

    QImage img;
    const QString ext = FileUtils::extensionOf(path);

    if (Constants::kImageExtensions.contains(ext)) {
        QImageReader r(path);
        r.setScaledSize(QSize(maxSize, maxSize));
        img = r.read();
    }
#ifdef DOCUSEARCH_HAS_POPPLER
    else if (ext == "pdf") {
        try {
            auto doc = poppler::document::load_from_file(path.toStdString());
            if (doc && doc->pages() > 0) {
                auto page = doc->create_page(0);
                if (page) {
                    poppler::page_renderer renderer;
                    renderer.set_render_hint(poppler::page_renderer::text_antialiasing);
                    const int dpi = 96;
                    // render_page takes poppler::page*. Depending on Poppler
                    // version, create_page() returns either unique_ptr<page>
                    // or page* directly. Handle both with 'if constexpr'.
                    using PageType = std::decay_t<decltype(page)>;
                    poppler::page* pagePtr = nullptr;
                    if constexpr (std::is_same_v<PageType, std::unique_ptr<poppler::page>>) {
                        pagePtr = page.get();
                    } else {
                        pagePtr = page;
                    }
                    auto img_data = renderer.render_page(pagePtr, dpi, dpi);
                    if (!img_data.is_valid()) return {};
                    // img_data.data() returns non-const char* in some versions,
                    // const char* in others. Cast away const if needed.
                    char* dataPtr = const_cast<char*>(img_data.data());
                    QImage tmp(reinterpret_cast<const uchar*>(dataPtr),
                               img_data.width(), img_data.height(),
                               img_data.bytes_per_row(),
                               QImage::Format_ARGB32);
                    img = tmp.scaled(maxSize, maxSize, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
                }
            }
        } catch (const std::exception& e) {
            DS_WARN("Preview", QString("Poppler exception: %1").arg(e.what()));
        }
    }
#endif
    // DOCX/XLSX/PPTX - skip thumbnails for now; UI shows a file-type icon.

    if (img.isNull()) return {};

    if (!img.save(cacheFile, "PNG")) {
        DS_WARN("Preview", "Failed to write cache: " + cacheFile);
    }
    return img;
}

void ThumbnailGenerator::clearCache() {
    QDir d(cacheDir_);
    d.removeRecursively();
    QDir().mkpath(cacheDir_);
}

} // namespace DocuSearch
