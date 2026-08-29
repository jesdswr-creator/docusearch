#pragma once

#include "IDocumentExtractor.h"

namespace DocuSearch {

// PDF extractor using PDFium (cpp bindings).
// If PDFium is unavailable, falls back to a stub that signals needsOcr.
class PdfExtractor : public IDocumentExtractor {
public:
    QStringList supportedExtensions() const override;
    ExtractionResult extract(const QString& path) override;
};

} // namespace DocuSearch
