#pragma once

#include "IDocumentExtractor.h"

namespace DocuSearch {

// Plain text, CSV, MD, RTF (simple strip-tags).
class TextExtractor : public IDocumentExtractor {
public:
    QStringList supportedExtensions() const override;
    ExtractionResult extract(const QString& path) override;
};

} // namespace DocuSearch
