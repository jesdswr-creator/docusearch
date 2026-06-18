// ============================================================
// FileUtils.cpp
// ============================================================

#include "FileUtils.h"
#include "Constants.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QCryptographicHash>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shlwapi.h>
#endif

#include <functional>

namespace DocuSearch {
namespace FileUtils {

QString extensionOf(const QString& path) {
    const int dot = path.lastIndexOf('.');
    if (dot < 0) return {};
    // Don't treat trailing dot as ext
    QString ext = path.mid(dot + 1).toLower();
    if (ext.contains('/') || ext.contains('\\')) return {};
    return ext;
}

bool hasExtension(const QString& path, const QStringList& exts) {
    const QString e = extensionOf(path);
    return exts.contains(e, Qt::CaseInsensitive);
}

bool isUnderAny(const QString& path, const QStringList& excludes) {
    // Normalize both path and exclude to forward slashes for comparison,
    // so the function works identically on Windows (backslash) and POSIX
    // (forward slash). We compare case-insensitively because Windows paths
    // are case-insensitive and the cost is negligible for short strings.
    auto normalize = [](QString s) {
        s = s.replace('\\', '/').toLower();
        if (!s.endsWith('/')) s += '/';
        return s;
    };
    const QString n = normalize(QDir::toNativeSeparators(path));
    for (const auto& ex : excludes) {
        if (n.startsWith(normalize(ex), Qt::CaseInsensitive)) return true;
    }
    return false;
}

QString toNative(const QString& path) {
    return QDir::toNativeSeparators(path);
}

void walkDirectory(const QString& root,
                   const QStringList& excludeDirs,
                   const std::function<bool(const QFileInfo&)>& callback) {
    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString abs = info.absoluteFilePath();

        // Skip excluded subtree entirely.
        if (isUnderAny(abs, excludeDirs)) continue;

        // Skip hidden / system on Windows
        if (info.isHidden()) continue;

        if (info.isDir()) continue; // we only emit files

        if (!callback(info)) return;
    }
}

QString sha256OfFile(const QString& path, qint64 maxBytes) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    constexpr qint64 kBuf = 1 << 16;
    QByteArray buf;
    qint64 total = 0;
    for (;;) {
        buf = f.read(kBuf);
        if (buf.isEmpty()) break;
        h.addData(buf);
        total += buf.size();
        if (maxBytes > 0 && total >= maxBytes) break;
    }
    return h.result().toHex();
}

QByteArray readHead(const QString& path, qint64 maxBytes) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.read(maxBytes);
}

QDateTime filetimeToQDateTime(quint64 filetime) {
    // FILETIME is 100-ns intervals since 1601-01-01 UTC
    const qint64 unix100ns = static_cast<qint64>(filetime) - 116444736000000000LL;
    const qint64 ms = unix100ns / 10000;
    return QDateTime::fromMSecsSinceEpoch(ms);
}

bool isReadableFile(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) return false;
    if (fi.isHidden()) return false;
    return fi.isReadable();
}

}} // namespace DocuSearch::Utils
