// ============================================================
// DocumentExtractorRegistry.cpp
// ============================================================

#include "DocumentExtractorRegistry.h"
#include "../core/Logger.h"
#include "../core/SehTranslator.h"
#include "TextExtractor.h"
#include "PdfExtractor.h"
#include "DocxExtractor.h"
#include "XlsxExtractor.h"
#include "PptxExtractor.h"

#include <QSet>

namespace DocuSearch {

DocumentExtractorRegistry& DocumentExtractorRegistry::instance() {
    static DocumentExtractorRegistry inst;
    return inst;
}

DocumentExtractorRegistry::DocumentExtractorRegistry() {
    registerExtractor(std::make_unique<TextExtractor>());
    registerExtractor(std::make_unique<PdfExtractor>());
    registerExtractor(std::make_unique<DocxExtractor>());
    registerExtractor(std::make_unique<XlsxExtractor>());
    registerExtractor(std::make_unique<PptxExtractor>());
}

void DocumentExtractorRegistry::registerExtractor(std::unique_ptr<IDocumentExtractor> ex) {
    const auto exts = ex->supportedExtensions();
    for (const auto& e : exts) byExt_[e.toLower()] = ex.get();
    owned_.push_back(std::move(ex));
}

IDocumentExtractor* DocumentExtractorRegistry::extractorFor(const QString& ext) const {
    auto it = byExt_.constFind(ext.toLower());
    return it == byExt_.constEnd() ? nullptr : it.value();
}

ExtractionResult DocumentExtractorRegistry::extractByExtension(const QString& path,
                                                               const QString& ext) const {
    // For image types we don't extract directly - OCR will handle them.
    static const QSet<QString> kImages = {"jpg","jpeg","png","tif","tiff","bmp","gif","webp"};
    if (kImages.contains(ext.toLower())) {
        ExtractionResult r;
        r.needsOcr = true;
        r.source = "ocr";
        return r;
    }

    auto* ex = extractorFor(ext);
    if (!ex) {
        ExtractionResult r;
        r.errorMessage = "No extractor for extension: " + ext;
        return r;
    }

    // ── SEH-safe extraction wrapper ──────────────────────────
    // Poppler, zlib and minizip can raise Win32 SEH exceptions
    // (access violations, stack overflows) on malformed files.
    // The SEH translator installed in main.cpp converts these into
    // catchable SehException (inherits std::exception). Combined
    // with the per-file try/catch in MainWindow::onExtract, this
    // makes extraction crash-proof: one bad file no longer takes
    // down the whole app.
    try {
        return ex->extract(path);
    } catch (const SehException& e) {
        DS_ERROR("Extract",
                 QString("SEH crash on %1: %2").arg(path).arg(e.what()));
        ExtractionResult r;
        r.errorMessage = QString("Structured exception: %1").arg(e.what());
        r.needsOcr = false;  // don't retry via OCR — file is malformed
        return r;
    } catch (const std::bad_alloc& e) {
        DS_ERROR("Extract",
                 QString("Out of memory on %1: %2").arg(path).arg(e.what()));
        ExtractionResult r;
        r.errorMessage = QString("Out of memory: %1").arg(e.what());
        return r;
    } catch (const std::exception& e) {
        DS_ERROR("Extract",
                 QString("Exception on %1: %2").arg(path).arg(e.what()));
        ExtractionResult r;
        r.errorMessage = QString("Exception: %1").arg(e.what());
        return r;
    } catch (...) {
        DS_ERROR("Extract", QString("Unknown exception on %1").arg(path));
        ExtractionResult r;
        r.errorMessage = "Unknown extraction failure";
        return r;
    }
}

} // namespace DocuSearch
