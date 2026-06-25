#pragma once

// ============================================================
// FileUtils.h - File-system helpers (cross-platform core, Win specials)
// ============================================================

#include <QString>
#include <QStringList>
#include <QFileInfo>

namespace DocuSearch {
namespace FileUtils {

// Returns lowercase extension without leading dot. Empty if none.
QString extensionOf(const QString& path);

// True if path's extension is in the given list (case-insensitive).
bool hasExtension(const QString& path, const QStringList& exts);

// Test whether `path` is under any of `excludes` (prefix match, case-insensitive).
bool isUnderAny(const QString& path, const QStringList& excludes);

// Convert to native separators.
QString toNative(const QString& path);

// Iterate a directory recursively. callback returns false to abort.
// Skips entries that match `excludeDirs` (absolute prefix match).
void walkDirectory(const QString& root,
                   const QStringList& excludeDirs,
                   const std::function<bool(const QFileInfo&)>& callback);

// Compute SHA-256 of a file (returns empty on error or skipped for large files
// depending on `maxBytes`; -1 = always hash).
QString sha256OfFile(const QString& path, qint64 maxBytes = -1);

// Read first `maxBytes` of a binary file into QByteArray.
QByteArray readHead(const QString& path, qint64 maxBytes);

// Convert Win32 FILETIME (uint64) to QDateTime.
QDateTime filetimeToQDateTime(quint64 filetime);

// Check if a path refers to an existing regular file (not directory, not hidden system).
bool isReadableFile(const QString& path);

}} // namespace DocuSearch::Utils
