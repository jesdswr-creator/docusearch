#pragma once

// ============================================================
// Constants.h - Application-wide compile-time constants
// ============================================================

#include <QString>
#include <cstdint>

namespace DocuSearch {
namespace Constants {

// Application
constexpr const char* kAppName        = "DocuSearch";
constexpr const char* kAppVersion     = "1.0.0";
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
inline const QStringList kDocumentExtensions = {
    "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "rtf", "csv", "md"
};

inline const QStringList kImageExtensions = {
    "jpg", "jpeg", "png", "tif", "tiff", "bmp", "gif", "webp"
};

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
