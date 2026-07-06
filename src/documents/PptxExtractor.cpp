// ============================================================
// PptxExtractor.cpp - read ALL slides from ppt/slides/slideN.xml
// ============================================================
//
// Strategy (same as XlsxExtractor):
//   1. Extract the entire PPTX ZIP to a temp dir in ONE PowerShell call.
//   2. Find all slide files matching ppt/slides/slide*.xml.
//   3. Parse each slide's <a:t> runs, prefixed with "--- Slide N ---".
//
// Previously this used a separate PowerShell call per slide (up to 200
// calls), which was extremely slow and could fail on large PPTX files
// due to cumulative timeouts. The new approach is ~100x faster.
// ============================================================

#include "PptxExtractor.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"

#include <QXmlStreamReader>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace DocuSearch {

namespace {

// Extract an entire ZIP into a temp directory using a SINGLE PowerShell
// call (Windows) or `unzip` (other platforms).
bool extractZipToDir(const QString& zipPath, const QString& outDir) {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    QString safeZip = zipPath;
    safeZip.replace("'", "''");
    QString safeOut = outDir;
    safeOut.replace("'", "''");
    const QString script = QString(
        "$ErrorActionPreference='SilentlyContinue';"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "[System.IO.Compression.ZipFile]::ExtractToDirectory('%1','%2');")
        .arg(safeZip, safeOut);
    proc.start("powershell", {"-NoProfile", "-Command", script});
#else
    proc.start("unzip", {"-o", "-q", zipPath, "-d", outDir});
#endif
    if (!proc.waitForStarted(3000)) return false;
    if (!proc.waitForFinished(60000)) { proc.kill(); return false; }
    return proc.exitCode() == 0;
}

// Read a file from the extracted dir.
QByteArray readFileFromDir(const QString& baseDir, const QString& relativePath) {
    QFile f(QDir(baseDir).absoluteFilePath(relativePath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

// Find all slide files in ppt/slides/ and return them sorted by slide number.
// Files are named slide1.xml, slide2.xml, etc.
QList<QPair<int, QString>> findSlideFiles(const QString& baseDir) {
    QList<QPair<int, QString>> out;
    const QString slidesDir = QDir(baseDir).absoluteFilePath("ppt/slides");
    QDir dir(slidesDir);
    if (!dir.exists()) return out;

    const QRegularExpression re("^slide(\\d+)\\.xml$");
    const QStringList files = dir.entryList(QStringList() << "slide*.xml", QDir::Files);
    for (const QString& fn : files) {
        const auto m = re.match(fn);
        if (m.hasMatch()) {
            const int num = m.captured(1).toInt();
            out.append({num, "ppt/slides/" + fn});
        }
    }
    // Sort by slide number.
    std::sort(out.begin(), out.end(),
              [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                  return a.first < b.first;
              });
    return out;
}

} // namespace

QStringList PptxExtractor::supportedExtensions() const {
    return {"pptx"};
}

ExtractionResult PptxExtractor::extract(const QString& path) {
    ExtractionResult r;
    r.source = "native";

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        r.errorMessage = "Failed to create temp dir for .pptx extraction.";
        return r;
    }

    // 1) Extract entire ZIP in ONE call (much faster than per-slide).
    if (!extractZipToDir(path, tmpDir.path())) {
        r.errorMessage = "Failed to extract .pptx ZIP.";
        DS_WARN("Pptx", r.errorMessage + " (path=" + path + ")");
        return r;
    }

    // 2) Find all slide files, sorted by slide number.
    const QList<QPair<int, QString>> slides = findSlideFiles(tmpDir.path());
    if (slides.isEmpty()) {
        r.errorMessage = "No slides found in .pptx";
        return r;
    }

    // 3) Parse each slide's <a:t> runs.
    QString text;
    for (const auto& s : slides) {
        const int slideNum = s.first;
        const QString relPath = s.second;
        const QByteArray xml = readFileFromDir(tmpDir.path(), relPath);
        if (xml.isEmpty()) continue;

        text.append("--- Slide " + QString::number(slideNum) + " ---\n");

        QXmlStreamReader xs(xml);
        while (!xs.atEnd()) {
            const auto tok = xs.readNext();
            if (tok == QXmlStreamReader::StartElement &&
                xs.name() == QStringLiteral("t")) {
                text.append(xs.readElementText()).append(' ');
            } else if (tok == QXmlStreamReader::StartElement &&
                       xs.name() == QStringLiteral("p")) {  // paragraph
                text.append('\n');
            }
        }
        text.append('\n');
    }

    if (text.isEmpty()) {
        r.errorMessage = "No text found in .pptx slides";
        return r;
    }

    r.text = Utils::stripControlChars(text);
    return r;
}

} // namespace DocuSearch
