// ============================================================
// XlsxExtractor.cpp — read xl/sharedStrings.xml + xl/worksheets/sheetN.xml
// ============================================================

#include "XlsxExtractor.h"
#include "DocxExtractor.h"  // for extractInnerFromZip (internal linkage, same TU?)
#include "../core/Logger.h"
#include "../core/StringUtils.h"

#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QProcess>
#include <QDir>
#include <QFile>

// We need access to extractInnerFromZip — but it's anonymous-namespace in
// DocxExtractor.cpp. Re-declare a local copy here for clarity.
namespace DocuSearch {

namespace {
QByteArray extractInnerFromZip2(const QString& zipPath, const QString& innerPath) {
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) return {};
    QProcess proc;
    proc.setWorkingDirectory(tmpDir.path());
    proc.setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
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

QStringList parseSharedStrings(const QByteArray& xml) {
    QStringList out;
    if (xml.isEmpty()) return out;
    QXmlStreamReader xs(xml);
    while (!xs.atEnd()) {
        const auto tok = xs.readNext();
        if (tok == QXmlStreamReader::StartElement && xs.name() == QStringLiteral("si")) {
            QString s;
            while (!xs.atEnd() &&
                   !(xs.tokenType() == QXmlStreamReader::EndElement &&
                     xs.name() == QStringLiteral("si"))) {
                const auto t = xs.readNext();
                if (t == QXmlStreamReader::StartElement && xs.name() == QStringLiteral("t")) {
                    s.append(xs.readElementText());
                }
            }
            out.append(s);
        }
    }
    return out;
}

QString parseSheet(const QByteArray& xml, const QStringList& shared) {
    QString text;
    QXmlStreamReader xs(xml);
    int lastRow = -1;
    while (!xs.atEnd()) {
        const auto tok = xs.readNext();
        if (tok != QXmlStreamReader::StartElement) continue;
        const auto name = xs.name();
        if (name != QStringLiteral("c")) continue;
        const QXmlStreamAttributes attrs = xs.attributes();
        const QString ref = attrs.value("r").toString();     // e.g., "A1"
        const QString t   = attrs.value("t").toString();     // "s" for shared
        // Extract row number from ref
        int row = 0;
        for (const QChar& c : ref) if (c.isDigit()) row = row * 10 + c.digitValue();

        // Find <v> child value
        QString v;
        while (!xs.atEnd() &&
               !(xs.tokenType() == QXmlStreamReader::EndElement &&
                 xs.name() == QStringLiteral("c"))) {
            const auto t2 = xs.readNext();
            if (t2 == QXmlStreamReader::StartElement && xs.name() == QStringLiteral("v")) {
                v = xs.readElementText();
            } else if (t2 == QXmlStreamReader::StartElement &&
                       xs.name() == QStringLiteral("is")) {
                // inline string
                while (!xs.atEnd() &&
                       !(xs.tokenType() == QXmlStreamReader::EndElement &&
                         xs.name() == QStringLiteral("is"))) {
                    if (xs.readNext() == QXmlStreamReader::StartElement &&
                        xs.name() == QStringLiteral("t"))
                        v.append(xs.readElementText());
                }
            }
        }
        if (v.isEmpty()) continue;
        if (t == "s") {
            bool ok = false;
            const int idx = v.toInt(&ok);
            if (ok && idx >= 0 && idx < shared.size()) v = shared[idx];
            else v.clear();
        }
        if (row != lastRow && lastRow != -1) text.append('\n');
        text.append(v).append('\t');
        lastRow = row;
    }
    return text;
}
} // namespace

QStringList XlsxExtractor::supportedExtensions() const {
    return {"xlsx"};
}

ExtractionResult XlsxExtractor::extract(const QString& path) {
    ExtractionResult r;
    r.source = "native";

    const QByteArray ssXml = extractInnerFromZip2(path, "xl/sharedStrings.xml");
    const QStringList shared = parseSharedStrings(ssXml);

    // Try sheets in order. xlsx always has xl/worksheets/sheet1.xml, often more.
    QString text;
    for (int i = 1; i <= 50; ++i) {
        const QString inner = QString("xl/worksheets/sheet%1.xml").arg(i);
        const QByteArray xml = extractInnerFromZip2(path, inner);
        if (xml.isEmpty()) break;
        text.append(parseSheet(xml, shared)).append('\n');
    }
    if (text.isEmpty()) {
        r.errorMessage = "No sheets found in .xlsx";
        return r;
    }
    r.text = Utils::stripControlChars(text);
    return r;
}

} // namespace DocuSearch
