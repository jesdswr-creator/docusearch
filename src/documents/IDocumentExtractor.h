#pragma once

// ============================================================
// IDocumentExtractor.h — Common interface for text extractors
// ============================================================

#include <QString>

namespace DocuSearch {

struct ExtractionResult {
    QString text;          // extracted plain text (UTF-8)
    QString source;        // "native" (no OCR needed), "ocr", "native+ocr"
    bool     needsOcr = false;  // hint that the page was likely scanned
    QString  errorMessage;
};

class IDocumentExtractor {
public:
    virtual ~IDocumentExtractor() = default;
    // Lowercase extensions without dot (e.g., "pdf", "docx").
    virtual QStringList supportedExtensions() const = 0;
    virtual ExtractionResult extract(const QString& path) = 0;
};

} // namespace DocuSearch
