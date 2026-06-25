#pragma once

#include "IDocumentExtractor.h"
#include <QHash>
#include <memory>

namespace DocuSearch {

// Central registry that maps file extensions to extractors.
class DocumentExtractorRegistry {
public:
    static DocumentExtractorRegistry& instance();

    void registerExtractor(std::unique_ptr<IDocumentExtractor> ex);

    // Returns the extractor for `ext` (lowercase, no dot) or nullptr.
    IDocumentExtractor* extractorFor(const QString& ext) const;

    // Convenience - calls extractorFor and returns result. If no extractor,
    // returns an empty result with needsOcr=true for image types.
    ExtractionResult extractByExtension(const QString& path, const QString& ext) const;

private:
    DocumentExtractorRegistry();
    QHash<QString, IDocumentExtractor*> byExt_;
    std::vector<std::unique_ptr<IDocumentExtractor>> owned_;
};

} // namespace DocuSearch
