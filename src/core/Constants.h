#pragma once

// ============================================================
// Constants.h - Application-wide compile-time constants
// ============================================================

#include <QString>
#include <QStringList>
#include <cstdint>

namespace DocuSearch {
namespace Constants {

// Application
constexpr const char* kAppName        = "DocuSearch";
constexpr const char* kAppVersion     = "1.7.11";  // v1.7.11: production-readiness pass — every Settings control now takes effect (excluded-extensions gate all three ingest paths, live-monitor checkbox honored, worker-thread count made honest), the frameless Close button runs closeEvent (geometry/settings persist, indexing guard), BGE init + batch-embedding workers stop-and-join on exit (UAF windows closed), the never-constructed legacy Indexer subsystem (~515 lines) deleted, EXE/MSIX metadata synced, daily logs pruned after 14 days, and repo junk (.env, TEST-RESULTS*, worklog) untracked. On top of that branch, this release: stats "Total size" includes -wal/-shm, the stats worker's raw sqlite connection got busy_timeout, dead config fields (includedExtensions/thumbnailSize/lastBackupPath) and the never-emitted refreshRequested wire removed. v1.7.10: duplicates fixed for real — the Add-Folder scan (scanFolderFast) NEVER wrote the hash column, so every row it created had hash='' and the duplicate finder honestly-but-uselessly reported "no duplicates"; it now fingerprints files during the scan (same SHA-256 + 64MB cap as the hourly walk), duplicates cover documents AND images (identical scanned jpg/png pairs are real duplicates), the empty-result message states how many files still lack fingerprints instead of a bare "no", and the startup hash backfill is 4000/launch. Rotated PDFs/images OCR fixed — auto-orientation triggered only when upright OCR read <3 words, but sideways pages still yield 3+ junk fragments so the rotated passes never ran; replaced with a quality score (chars in 3+ letter runs, threshold 48) for PDFs AND images (images had no rotation retry at all), OCR rasters bounded to 2600px (Windows OCR accuracy cliff), failed rows get one honest retry per launch, and the OCR temp-file name collision between the two workers is fixed. New: Settings → Backup & Restore → "Remove Database (Reset)" wipes the index database (never user files), rebuilds schema and re-scans automatically; first run now extracts EVERYTHING automatically (200-file sessions, 3s re-arm) until the first full drain completes. v1.7.9: extraction + duplicates wiring repaired — the OCR worker pool was declared but NEVER constructed or enqueued (needs_ocr PDFs and ALL images were stranded and "Extract" said nothing to do); Extract now gathers needs_ocr + image rows and runs them on the pool; startup integrity pass requeues fake-done rows (pre-v1.7.3 scans stamped content_done without extracting) and backfills missing hashes so duplicates works; DocumentText always written (empty text = done). v1.7.8: startup un-blocked — the one-time non-document purge no longer runs before the window shows (on big legacy indexes it froze the splash for minutes, and killing the app mid-purge rolled the single transaction back so the NEXT launch froze too — the "stuck at splash" loop); purge now prepared-once + 500-row batched commits + event pumps, scheduled t+4.5 s; splash drops after 30 s so nothing can ever hide behind it; single-instance guard (second launch shows a visible message instead of fighting the running instance for the database). v1.7.7: ONE extension allowlist (kIndexableExtensions) now gates EVERY ingest path - hourly scan, FileWatcher, Add-Folder scan, full re-index - so .md/.txt/.csv/.rtf/.log and binary types never enter the index, and a one-time startup purge removes previously indexed non-documents; splash bar replaced by a smooth material sweep (no bounce stutter) with crossfaded captions and paced event pumps at every startup milestone; duplicates: document-only hash grouping + display-time existence re-verification (a file whose partner was moved/deleted can never render as a pair). v1.7.6: PDF preview use-after-free fixed (PDFium memory-buffer contract - the bytes now live with the document; flaky "error opening PDF" and garbled OCR/text gone); OCR auto-orients scans stored sideways (90/180/270 fallback via native PDFium rotation); duplicates: results list cleared when nothing found + stale-hash gate drops files whose content changed since scanning; theme wiring fixed (saved dark mode actually applies + toggle persists); splash matches the active theme (button-color accent). v1.7.5: AI fusion strictly additive; canonical duplicate identity; live edit indexing; PDF preview error overlay; splash animation under the event loop; real logo
constexpr const char* kOrgName        = "DocuSearch";
constexpr const char* kOrgDomain      = "docusearch.local";

// Database
constexpr const char* kDbFileName     = "docusearch.db";
constexpr const char* kBackupSuffix   = ".bak";

// Indexing
constexpr int     kDefaultWorkerThreads    = 2;
constexpr int     kMaxWorkerThreads        = 16;
constexpr int     kDefaultCpuThresholdPct  = 70;   // pause indexing above this
constexpr int     kDefaultCpuTargetPct     = 30;   // try to keep CPU around this
constexpr int     kBatchSize               = 500;  // DB transaction batch
constexpr qint64  kLazyOcrTimeoutMs        = 30000;
constexpr int     kFileWatcherBufferBytes  = 65536;

// Performance limits for low-end systems (4GB RAM)
constexpr int     kMaxPdfPreviewPages      = 30;    // max pages rendered for preview
constexpr int     kMaxPdfOcrPages          = 20;    // max pages OCR'd per file
constexpr int     kMaxExtractTextChars     = 500000; // ~500KB text cap per file
constexpr int     kPdfPreviewDpi           = 96;    // DPI for PDF preview rendering
constexpr int     kPdfOcrDpi               = 150;   // DPI for OCR (lower = faster, less memory)
constexpr int     kExtractionBatchSize     = 50;    // files per extraction batch (memory)
constexpr qint64  kMaxFilesizeToExtract    = 100 * 1024 * 1024; // 100MB max for extraction

// Priority bands (days since modified)
constexpr int kPriority1Days = 30;
constexpr int kPriority2Days = 365;

// Hashing
constexpr qint64 kHashFilesLargerThanBytes = 50 * 1024 * 1024; // 50 MB

// UI
constexpr int kSearchDebounceMs        = 150;
constexpr int kPreviewMaxChars         = 50000;
constexpr int kThumbnailMaxSize        = 512;
constexpr int kSearchResultSnippetLen  = 200;

// Supported file extensions (lowercase, no dot)
// v1.7.7: trimmed to formats the app can actually open, preview,
// extract or OCR. .md/.txt/.csv/.log/.rtf and every unknown binary
// type are deliberately NOT here - see kIndexableExtensions below,
// which is the ONE allowlist every ingest path must consult.
inline const QStringList kDocumentExtensions = {
    "pdf", "doc", "docx", "xls", "xlsx", "xlsm", "ppt", "pptx"
};

inline const QStringList kImageExtensions = {
    "jpg", "jpeg", "png", "tif", "tiff", "bmp", "gif", "webp"
};

// v1.7.7 - THE allowlist. Every ingest path (hourly reconciliation
// scan, FileWatcher add/modify, Add-Folder quick scan, full re-index)
// must gate on this, and the one-time startup purge deletes rows whose
// extension is not in it. Result: search results and the "N indexed"
// badge contain only real documents and images - no .md notes, no
// installers, no archives, no OS junk.
inline const QStringList kIndexableExtensions = {
    "pdf", "doc", "docx", "xls", "xlsx", "xlsm", "ppt", "pptx",
    "jpg", "jpeg", "png", "tif", "tiff", "bmp", "gif", "webp",
};

inline bool isIndexableExtension(const QString& ext) {
    static const QSet<QString> set = [] {
        QSet<QString> s;
        for (const QString& e : kIndexableExtensions) s.insert(e.toLower());
        return s;
    }();
    return set.contains(ext.toLower());
}

inline const QStringList kIgnoredExtensions = {
    "exe", "dll", "sys", "so", "dylib", "obj", "lib",
    "mp3", "wav", "flac", "aac", "ogg",
    "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm",
    "iso", "bin", "nrg", "img",
    "zip", "rar", "7z", "tar", "gz", "bz2", "xz",
    "tmp", "temp", "log", "bak", "old"
};

// Indexing statuses
namespace IndexingStatus {
    constexpr const char* kPending       = "pending";
    constexpr const char* kMetadataOnly  = "metadata_only";
    constexpr const char* kContentDone   = "content_done";
    constexpr const char* kOcrDone       = "ocr_done";
    constexpr const char* kFailed        = "failed";
    constexpr const char* kSkipped       = "skipped";
}

namespace OcrStatus {
    constexpr const char* kPending  = "pending";
    constexpr const char* kRunning  = "running";
    constexpr const char* kDone     = "done";
    constexpr const char* kFailed   = "failed";
    constexpr const char* kSkipped  = "skipped";
    constexpr const char* kNotNeeded= "not_needed";
}

} // namespace Constants
} // namespace DocuSearch
