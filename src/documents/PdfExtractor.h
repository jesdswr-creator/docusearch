#pragma once

#include "IDocumentExtractor.h"

namespace DocuSearch {

// PDF extractor using Poppler (cpp bindings).
// If Poppler is unavailable, falls back to a stub that signals needsOcr.
class PdfExtractor : public IDocumentExtractor {
public:
    QStringList supportedExtensions() const override;
    ExtractionResult extract(const QString& path) override;
};

} // namespace DocuSearch
