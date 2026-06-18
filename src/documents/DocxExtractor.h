#pragma once

#include "IDocumentExtractor.h"

namespace DocuSearch {

// Microsoft Word .docx (Office Open XML).
// Reads word/document.xml inside the package and extracts <w:t> text.
class DocxExtractor : public IDocumentExtractor {
public:
    QStringList supportedExtensions() const override;
    ExtractionResult extract(const QString& path) override;
};

} // namespace DocuSearch
