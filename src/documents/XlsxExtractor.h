#pragma once

#include "IDocumentExtractor.h"

namespace DocuSearch {

// Microsoft Excel .xlsx — read shared strings + sheet cells.
class XlsxExtractor : public IDocumentExtractor {
public:
    QStringList supportedExtensions() const override;
    ExtractionResult extract(const QString& path) override;
};

} // namespace DocuSearch
