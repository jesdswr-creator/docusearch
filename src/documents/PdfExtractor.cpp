// ============================================================
// PdfExtractor.cpp — uses poppler-cpp when available
// ============================================================

#include "PdfExtractor.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"

#ifdef DOCUSEARCH_HAS_POPPLER
#  include <poppler-document.h>
#  include <poppler-page.h>
#  include <poppler-global.h>
#endif

#include <QFile>

namespace DocuSearch {

QStringList PdfExtractor::supportedExtensions() const {
    return {"pdf"};
}

ExtractionResult PdfExtractor::extract(const QString& path) {
    ExtractionResult r;
    r.source = "native";

#ifdef DOCUSEARCH_HAS_POPPLER
    try {
        auto doc = poppler::document::load_from_file(path.toStdString());
        if (!doc) {
            r.errorMessage = "Poppler failed to open PDF";
            r.needsOcr = true;
            return r;
        }
        const int n = doc->pages();
        const int maxPages = std::min(n, 200); // cap for performance
        QString all;
        all.reserve(64 * 1024);
        int emptyPages = 0;
        for (int i = 0; i < maxPages; ++i) {
            auto p = doc->create_page(i);
            if (!p) continue;
            const std::string txt = p->text();
            const QString q = QString::fromUtf8(txt.data(), static_cast<int>(txt.size()));
            if (q.trimmed().isEmpty()) ++emptyPages;
            all.append(q);
            all.append('\n');
            if (all.size() > 1'000'000) break; // cap text size
        }
        r.text = Utils::stripControlChars(all);
        // If >50% of pages are empty, mark as scanned → OCR needed.
        if (maxPages > 0 && emptyPages * 2 > maxPages) r.needsOcr = true;
    } catch (const std::exception& e) {
        r.errorMessage = QString("Poppler exception: %1").arg(e.what());
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
    r.errorMessage = "Poppler not linked — PDF text extraction unavailable";
    r.needsOcr = true;
#endif
    return r;
}

} // namespace DocuSearch
