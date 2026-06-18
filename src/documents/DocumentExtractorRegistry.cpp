// ============================================================
// DocumentExtractorRegistry.cpp
// ============================================================

#include "DocumentExtractorRegistry.h"
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
    // For image types we don't extract directly — OCR will handle them.
    static const QSet<QString> kImages = {"jpg","jpeg","png","tif","tiff","bmp","gif","webp"};
    if (kImages.contains(ext.toLower())) {
        ExtractionResult r;
        r.needsOcr = true;
        r.source = "ocr";
        return r;
    }
    if (auto* ex = extractorFor(ext)) return ex->extract(path);
    ExtractionResult r;
    r.errorMessage = "No extractor for extension: " + ext;
    return r;
}

} // namespace DocuSearch
