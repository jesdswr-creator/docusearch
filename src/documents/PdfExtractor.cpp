// ============================================================
// PdfExtractor.cpp - PDF text extraction via PDFium
// ============================================================

#include "PdfExtractor.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"

#ifdef DOCUSEARCH_HAS_PDFIUM
#  include "../pdf/PdfiumDocument.h"
#endif

#include <QFile>

namespace DocuSearch {

QStringList PdfExtractor::supportedExtensions() const {
    return {"pdf"};
}

ExtractionResult PdfExtractor::extract(const QString& path) {
    ExtractionResult r;
    r.source = "native";

#ifdef DOCUSEARCH_HAS_PDFIUM
    try {
        PdfiumDocument doc;
        if (!doc.loadFromFile(path)) {
            r.errorMessage = doc.lastError().isEmpty()
                ? QStringLiteral("PDF engine failed to open PDF")
                : doc.lastError();
            r.needsOcr = true;
            return r;
        }
        const int n = doc.pageCount();
        const int maxPages = std::min(n, 200); // cap for performance
        QString all;
        all.reserve(64 * 1024);
        int emptyPages = 0;
        for (int i = 0; i < maxPages; ++i) {
            // PDFium returns the page's native UTF-16 text layer; scan-only
            // pages come back empty and feed the needsOcr heuristic below.
            const QString q = doc.pageText(i, 200000);
            if (q.trimmed().isEmpty()) ++emptyPages;
            all.append(q);
            all.append('\n');
            if (all.size() > 500'000) break; // cap text size (~500KB) for memory
        }
        r.text = Utils::stripControlChars(all);
        if (maxPages > 0 && emptyPages * 2 > maxPages) r.needsOcr = true;
    } catch (const std::exception& e) {
        r.errorMessage = QString("PDF exception: %1").arg(e.what());
        DS_WARN("Pdf", r.errorMessage);
    }
#else
    // Fallback: open file and probe for "%PDF-" header.
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray head = f.read(8);
        if (!head.startsWith("%PDF")) {
            r.errorMessage = "Not a PDF file";
            return r;
        }
    }
    r.errorMessage = "PDF engine not linked - PDF text extraction unavailable";
    r.needsOcr = true;
#endif
    return r;
}

} // namespace DocuSearch
