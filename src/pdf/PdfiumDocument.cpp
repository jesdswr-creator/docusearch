// ============================================================
// PdfiumDocument.cpp - PDFium-backed PDF engine
// ============================================================
//
// Why PDFium: Poppler-cpp is GPL — a hard blocker for commercial
// distribution. PDFium (Chromium's PDF engine, BSD-3-style via
// bblanchon/pdfium-binaries) renders faster, exposes exact page
// geometry (no more low-DPI render probes like the Poppler path
// needed), and has zero transitive DLL dependencies.
//
// Buffer format: FPDFBitmap_BGRA is byte-ordered B,G,R,A in memory,
// which matches QImage::Format_ARGB32 on little-endian — we can wrap
// the bitmap buffer directly and copy() to detach.
// ============================================================

#include "PdfiumDocument.h"

#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>
#include <QThread>

#include <fpdfview.h>
#include <fpdf_text.h>

#include <vector>

namespace DocuSearch {

namespace {
// PDFium is not thread-safe; serialize every call process-wide.
QRecursiveMutex& pdfiumMutex() {
    static QRecursiveMutex m;
    return m;
}
} // namespace

void PdfiumDocument::ensureLibrary() {
    QMutexLocker lock(&pdfiumMutex());
    static const bool initialized = []() {
        FPDF_InitLibrary();
        return true;
    }();
    Q_UNUSED(initialized);
}

PdfiumDocument::~PdfiumDocument() {
    if (m_doc) {
        QMutexLocker lock(&pdfiumMutex());
        FPDF_CloseDocument(static_cast<FPDF_DOCUMENT>(m_doc));
        m_doc = nullptr;
    }
}

bool PdfiumDocument::loadFromFile(const QString& path) {
    ensureLibrary();
    QMutexLocker lock(&pdfiumMutex());

    if (m_doc) {
        FPDF_CloseDocument(static_cast<FPDF_DOCUMENT>(m_doc));
        m_doc = nullptr;
    }
    m_lastError.clear();

    // Load through memory: sidesteps PDFium's platform-specific path
    // encoding on Windows (UTF-8 vs UTF-16 filesystem paths) and lets
    // us distinguish "file missing" from "file corrupt" precisely.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        // v1.7.5: a failed open is frequently transient — antivirus
        // engines and cloud-sync providers briefly hold new files. One
        // short retry removes a whole class of spurious "error opening
        // pdf preview" reports; if it still fails, report the lock
        // explicitly so the user knows it is not a corrupt file.
        QThread::msleep(120);
        if (!f.open(QIODevice::ReadOnly)) {
            m_lastError = QFileInfo(path).exists()
                ? QStringLiteral("File is in use by another program (locked)")
                : QStringLiteral("File not found or locked");
            return false;
        }
    }
    const QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        m_lastError = "File is empty";
        return false;
    }

    m_doc = FPDF_LoadMemDocument(bytes.constData(),
                                 static_cast<size_t>(bytes.size()),
                                 /*password=*/nullptr);
    if (!m_doc) {
        // Some PDFs carry an OWNER password but open freely — mirror the
        // old Poppler path by retrying once with an empty password.
        const unsigned long err = FPDF_GetLastError();
        if (err == FPDF_ERR_PASSWORD) {
            m_doc = FPDF_LoadMemDocument(bytes.constData(),
                                         static_cast<size_t>(bytes.size()),
                                         /*password=*/"");
        }
        if (!m_doc) {
            switch (FPDF_GetLastError()) {
                case FPDF_ERR_PASSWORD: m_lastError = "Password-protected PDF"; break;
                case FPDF_ERR_FORMAT:   m_lastError = "Corrupt or unsupported PDF"; break;
                case FPDF_ERR_SECURITY: m_lastError = "PDF blocked by security handler"; break;
                case FPDF_ERR_FILE:     m_lastError = "File read error"; break;
                default:                m_lastError = "Failed to open PDF"; break;
            }
            return false;
        }
    }
    return true;
}

int PdfiumDocument::pageCount() const {
    if (!m_doc) return 0;
    QMutexLocker lock(&pdfiumMutex());
    return FPDF_GetPageCount(static_cast<FPDF_DOCUMENT>(m_doc));
}

QSizeF PdfiumDocument::pageSizePoints(int index) const {
    if (!m_doc) return QSizeF(595.0, 842.0);   // A4 portrait fallback
    QMutexLocker lock(&pdfiumMutex());
    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_doc), index);
    if (!page) return QSizeF(595.0, 842.0);
    const double w = FPDF_GetPageWidth(page);
    const double h = FPDF_GetPageHeight(page);
    FPDF_ClosePage(page);
    if (w <= 1.0 || h <= 1.0) return QSizeF(595.0, 842.0);
    return QSizeF(w, h);
}

QImage PdfiumDocument::renderPage(int index, double dpi) const {
    if (!m_doc || dpi <= 0.0) return QImage();
    QMutexLocker lock(&pdfiumMutex());

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_doc), index);
    if (!page) return QImage();

    const double wPt = FPDF_GetPageWidth(page);
    const double hPt = FPDF_GetPageHeight(page);
    if (wPt <= 1.0 || hPt <= 1.0) { FPDF_ClosePage(page); return QImage(); }

    int w = static_cast<int>(wPt / 72.0 * dpi + 0.5);
    int h = static_cast<int>(hPt / 72.0 * dpi + 0.5);
    // Safety valve: refuse absurd rasters (> ~34 MP) instead of trying
    // to allocate them — the old Poppler path simply OOMed.
    if (w < 1 || h < 1 || (qint64)w * (qint64)h > 34'000'000) {
        FPDF_ClosePage(page);
        return QImage();
    }

    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(
        w, h, FPDFBitmap_BGRA, /*buffer=*/nullptr, /*stride=*/0);
    if (!bmp) { FPDF_ClosePage(page); return QImage(); }
    // PDFium rasterizes onto whatever is in the buffer — start opaque
    // white so pages with transparent backgrounds read correctly.
    FPDFBitmap_FillRect(bmp, 0, 0, w, h, 0xFFFFFFFF);

    FPDF_RenderPageBitmap(bmp, page,
                          /*start_x=*/0, /*start_y=*/0,
                          /*size_x=*/w, /*size_y=*/h,
                          /*rotate=*/0, /*flags=*/FPDF_ANNOT);

    QImage qimg(static_cast<const uchar*>(FPDFBitmap_GetBuffer(bmp)),
                w, h, FPDFBitmap_GetStride(bmp),
                QImage::Format_ARGB32);
    QImage out = qimg.copy();          // detach before the buffer dies

    FPDFBitmap_Destroy(bmp);
    FPDF_ClosePage(page);
    return out;
}

QString PdfiumDocument::pageText(int index, int maxChars) const {
    if (!m_doc) return QString();
    QMutexLocker lock(&pdfiumMutex());

    FPDF_PAGE page = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(m_doc), index);
    if (!page) return QString();

    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) { FPDF_ClosePage(page); return QString(); }

    QString out;
    const int total = FPDFText_CountChars(tp);
    if (total > 0) {
        const int count = qMin(total, maxChars);
        // GetText writes UTF-16LE and NUL-terminates; reserve one extra.
        std::vector<unsigned short> buf(static_cast<size_t>(count) + 1,
                                        0u);
        const int written = FPDFText_GetText(tp, /*start_index=*/0,
                                             count, buf.data());
        if (written > 0) {
            int len = written;
            if (buf[static_cast<size_t>(len) - 1] == 0u) --len;  // drop NUL
            out = QString::fromUtf16(
                reinterpret_cast<const char16_t*>(buf.data()), len);
        }
    }

    FPDFText_ClosePage(tp);
    FPDF_ClosePage(page);
    return out;
}

} // namespace DocuSearch
