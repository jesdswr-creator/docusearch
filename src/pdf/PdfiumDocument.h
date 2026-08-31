#pragma once

// ============================================================
// PdfiumDocument.h - RAII wrapper around the PDFium C API
// ============================================================
//
// Replaces Poppler (GPL) as the PDF engine. PDFium is BSD-3-style
// licensed (via the bblanchon/pdfium-binaries builds), so the app can
// be distributed AND sold without GPL obligations.
//
// One wrapper instance = one open document. All calls are serialized
// by a process-wide recursive mutex because PDFium is NOT thread-safe;
// the app currently renders/extracts on the main thread only, but the
// guard keeps a future worker thread from corrupting global state.
//
// The library is initialized lazily on first use and never torn down
// (process-lifetime singleton — same pattern the ONNX runtime uses).
// ============================================================

#include <QString>
#include <QImage>
#include <QSizeF>
#include <QByteArray>

namespace DocuSearch {

class PdfiumDocument {
public:
    PdfiumDocument() = default;
    ~PdfiumDocument();

    PdfiumDocument(const PdfiumDocument&)            = delete;
    PdfiumDocument& operator=(const PdfiumDocument&) = delete;

    // Opens a PDF from disk. On failure returns false and lastError()
    // carries a human-readable reason ("Password-protected PDF", ...).
    // The file is read into memory and the buffer is KEPT ALIVE for the
    // document's lifetime: PDFium's FPDF_LoadMemDocument reads the buffer
    // lazily and documents that it "must remain valid when the document
    // is open" — freeing it after load corrupts every later render/text
    // call (the v1.7.6 root-cause fix for flaky "error opening PDF").
    bool loadFromFile(const QString& path);

    bool isValid() const { return m_doc != nullptr; }
    const QString& lastError() const { return m_lastError; }

    // Total page count (0 when not loaded).
    int pageCount() const;

    // Page size in PDF points (1/72"). Falls back to A4 portrait if
    // the page cannot be loaded — callers only use this for layout.
    // PDFium applies the page's /Rotate to these values (they are the
    // DISPLAY dimensions), so callers never need to swap them.
    QSizeF pageSizePoints(int index) const;

    // Rasterizes the page at the requested DPI (72 = native PDF units).
    // White background, anti-aliased, annotation layer included.
    // The page's own /Rotate is ALWAYS applied by PDFium, so the result
    // is upright. `rotate` rotates the raster ADDITIONALLY (OCR uses this
    // to auto-orient scans that were stored sideways without a /Rotate
    // entry): 0 = none, 1 = 90° CW, 2 = 180°, 3 = 90° CCW.
    // Returns a null QImage on failure.
    QImage renderPage(int index, double dpi, int rotate = 0) const;

    // Extracts the page's text layer (UTF-16 natively). Empty when the
    // page is scan-only. maxChars caps pathological single pages.
    QString pageText(int index, int maxChars = 200000) const;

    // Idempotent FPDF_InitLibrary. Called internally; public so main()
    // may warm it up early if needed.
    static void ensureLibrary();

private:
    void* m_doc = nullptr;      // FPDF_DOCUMENT, kept void* here so the
                                // header does not need fpdfview.h
    // v1.7.6: the raw PDF bytes, kept alive for the WHOLE document
    // lifetime. FPDF_LoadMemDocument wraps this buffer in a stream and
    // reads it lazily while rendering/extracting text — the public
    // contract says "the memory buffer must remain valid when the
    // document is open". Before v1.7.6 the bytes were a local in
    // loadFromFile() and died at return: every later page operation
    // was a use-after-free (flaky "error opening PDF", garbled pages,
    // garbage OCR text).
    QByteArray m_fileBytes;
    QString m_lastError;
};

} // namespace DocuSearch
