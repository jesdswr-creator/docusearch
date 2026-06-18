// ============================================================
// PptxExtractor.cpp — read ppt/slides/slideN.xml <a:t> runs
// ============================================================

#include "PptxExtractor.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"

#include <QXmlStreamReader>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>

namespace DocuSearch {

namespace {
QByteArray extractInnerFromZip3(const QString& zipPath, const QString& innerPath) {
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return {};
    QProcess proc;
    proc.setWorkingDirectory(tmpDir.path());
    proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    // Make a mutable copy of zipPath so we can escape single quotes for PowerShell.
    QString safeZip = zipPath;
    safeZip.replace("'", "''");
    const QString script = QString(
        "$ErrorActionPreference='SilentlyContinue';"
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$z=[System.IO.Compression.ZipFile]::OpenRead('%1');"
        "$e=$z.Entries | Where-Object {$_.FullName -eq '%2'};"
        "if($e){$s=New-Object System.IO.MemoryStream;$e.Open().CopyTo($s);"
        "[System.IO.File]::WriteAllBytes('%3\\extracted.bin',$s.ToArray())}"
        "$z.Dispose();").arg(safeZip, innerPath, tmpDir.path());
    proc.start("powershell", {"-NoProfile", "-Command", script});
#else
    proc.start("unzip", {"-p", zipPath, innerPath});
#endif
    if (!proc.waitForStarted(3000)) return {};
    if (!proc.waitForFinished(15000)) { proc.kill(); return {}; }
#ifdef Q_OS_WIN
    QFile out(tmpDir.path() + "/extracted.bin");
    if (out.open(QIODevice::ReadOnly)) return out.readAll();
    return {};
#else
    return proc.readAllStandardOutput();
#endif
}
} // namespace

QStringList PptxExtractor::supportedExtensions() const {
    return {"pptx"};
}

ExtractionResult PptxExtractor::extract(const QString& path) {
    ExtractionResult r;
    r.source = "native";
    QString text;
    for (int i = 1; i <= 200; ++i) {
        const QString inner = QString("ppt/slides/slide%1.xml").arg(i);
        const QByteArray xml = extractInnerFromZip3(path, inner);
        if (xml.isEmpty()) break;

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
        r.errorMessage = "No slides found in .pptx";
        return r;
    }
    r.text = Utils::stripControlChars(text);
    return r;
}

} // namespace DocuSearch
