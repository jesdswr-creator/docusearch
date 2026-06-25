#pragma once

#include "IDocumentExtractor.h"

namespace DocuSearch {

// PowerPoint .pptx - read ppt/slides/slideN.xml text runs.
class PptxExtractor : public IDocumentExtractor {
public:
    QStringList supportedExtensions() const override;
    ExtractionResult extract(const QString& path) override;
};

} // namespace DocuSearch
