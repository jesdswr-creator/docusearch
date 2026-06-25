// ============================================================
// XlsxExtractor.cpp - read ALL sheets from .xlsx/.xlsm
// ============================================================
//
// Strategy:
//   1. Extract the entire XLSX ZIP to a temp dir in ONE PowerShell call
//      (much faster than one call per inner file).
//   2. Parse xl/workbook.xml for the ordered list of sheets - each sheet
//      has a name and an r:id (relationship id).
//   3. Parse xl/_rels/workbook.xml.rels for the r:id -> file path map
//      (so we handle non-standard sheet ordering / names).
//   4. Read each sheet, prefixed with "--- SheetName ---".
//   5. Fallback: if workbook.xml parsing fails, brute-force sheets 1..100.
//
// Supports both .xlsx and .xlsm (macro-enabled).

#include "XlsxExtractor.h"
#include "../core/Logger.h"
#include "../core/StringUtils.h"

#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QHash>
#include <QPair>

namespace DocuSearch {

namespace {

// Extract an entire XLSX ZIP into a temp directory using a SINGLE PowerShell
// call (Windows) or `unzip` (other platforms). Returns the temp dir path
// (caller must hold the QTemporaryDir to keep files alive).
//
// On Windows the Expand-Archive cmdlet cannot stream into memory; we use
// [System.IO.Compression.ZipFile]::ExtractToDirectory for speed and to avoid
// the per-entry PowerShell overhead.
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

// Read the file at `relativePath` (e.g., "xl/workbook.xml") from the
// extracted dir. Returns empty QByteArray on failure.
QByteArray readFileFromDir(const QString& baseDir, const QString& relativePath) {
    QFile f(QDir(baseDir).absoluteFilePath(relativePath));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

// Parse xl/workbook.xml and return ordered list of (sheetName, rId) pairs.
QList<QPair<QString, QString>> parseWorkbookSheets(const QByteArray& xml) {
    QList<QPair<QString, QString>> out;
    if (xml.isEmpty()) return out;
    QXmlStreamReader xs(xml);
    while (!xs.atEnd()) {
        const auto tok = xs.readNext();
        if (tok == QXmlStreamReader::StartElement &&
            xs.name() == QStringLiteral("sheet")) {
            const QXmlStreamAttributes a = xs.attributes();
            const QString name = a.value("name").toString();
            // The relationship id can be in r:id or rs:id (some writers).
            QString rId = a.value("http://schemas.openxmlformats.org/officeDocument/2006/relationships", "id").toString();
            if (rId.isEmpty()) rId = a.value("r:id").toString();
            if (rId.isEmpty()) rId = a.value("rs:id").toString();
            if (!name.isEmpty()) out.append({name, rId});
        }
    }
    return out;
}

// Parse xl/_rels/workbook.xml.rels: map rId -> target file path
// (e.g., "rId1" -> "worksheets/sheet1.xml").
QHash<QString, QString> parseWorkbookRels(const QByteArray& xml) {
    QHash<QString, QString> out;
    if (xml.isEmpty()) return out;
    QXmlStreamReader xs(xml);
    while (!xs.atEnd()) {
        const auto tok = xs.readNext();
        if (tok == QXmlStreamReader::StartElement &&
            xs.name() == QStringLiteral("Relationship")) {
            const QXmlStreamAttributes a = xs.attributes();
            const QString id = a.value("Id").toString();
            const QString target = a.value("Target").toString();
            if (!id.isEmpty() && !target.isEmpty()) {
                // Some Targets are like "worksheets/sheet1.xml" (relative to xl/),
                // others are absolute "/xl/worksheets/sheet1.xml". Normalize.
                QString t = target;
                if (t.startsWith('/')) t = t.mid(1);  // strip leading slash
                out.insert(id, t);
            }
        }
    }
    return out;
}

// Parse shared strings (unchanged from original).
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

// Parse a single sheet XML; returns tab-separated rows with newline between rows.
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
        // Extract row number from ref.
        int row = 0;
        for (const QChar& c : ref) if (c.isDigit()) row = row * 10 + c.digitValue();

        // Find <v> child value.
        QString v;
        while (!xs.atEnd() &&
               !(xs.tokenType() == QXmlStreamReader::EndElement &&
                 xs.name() == QStringLiteral("c"))) {
            const auto t2 = xs.readNext();
            if (t2 == QXmlStreamReader::StartElement && xs.name() == QStringLiteral("v")) {
                v = xs.readElementText();
            } else if (t2 == QXmlStreamReader::StartElement &&
                       xs.name() == QStringLiteral("is")) {
                // Inline string.
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
        if (v.isEmpty()) continue;
        if (row != lastRow && lastRow != -1) text.append('\n');
        text.append(v).append('\t');
        lastRow = row;
    }
    return text;
}

} // namespace

QStringList XlsxExtractor::supportedExtensions() const {
    return {"xlsx", "xlsm"};
}

ExtractionResult XlsxExtractor::extract(const QString& path) {
    ExtractionResult r;
    r.source = "native";

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        r.errorMessage = "Failed to create temp dir for .xlsx extraction.";
        return r;
    }

    // 1) Extract entire ZIP in ONE PowerShell/unzip call.
    if (!extractZipToDir(path, tmpDir.path())) {
        r.errorMessage = "Failed to extract .xlsx ZIP.";
        DS_WARN("Xlsx", r.errorMessage + " (path=" + path + ")");
        return r;
    }

    // 2) Parse shared strings.
    const QStringList shared =
        parseSharedStrings(readFileFromDir(tmpDir.path(), "xl/sharedStrings.xml"));

    // 3) Parse workbook.xml + rels to map sheet names -> file paths.
    const QByteArray wbXml   = readFileFromDir(tmpDir.path(), "xl/workbook.xml");
    const QByteArray relsXml = readFileFromDir(tmpDir.path(), "xl/_rels/workbook.xml.rels");
    const QList<QPair<QString, QString>> sheets = parseWorkbookSheets(wbXml);
    const QHash<QString, QString> relMap = parseWorkbookRels(relsXml);

    QString text;

    if (!sheets.isEmpty() && !relMap.isEmpty()) {
        // 4a) Read each sheet, prefixed with "--- SheetName ---".
        for (const auto& s : sheets) {
            const QString& sheetName = s.first;
            const QString& rId       = s.second;
            const QString relTarget  = relMap.value(rId);
            if (relTarget.isEmpty()) continue;
            // relTarget is relative to xl/. Build the full path inside the
            // extracted dir.
            QString sheetRel = relTarget;
            if (sheetRel.startsWith("xl/")) sheetRel = sheetRel.mid(3);
            const QByteArray sheetXml =
                readFileFromDir(tmpDir.path(), "xl/" + sheetRel);
            if (sheetXml.isEmpty()) continue;
            text.append("--- " + sheetName + " ---\n");
            text.append(parseSheet(sheetXml, shared)).append('\n');
        }
    } else {
        // 4b) Fallback: brute-force sheets 1..100 if workbook.xml parsing fails.
        for (int i = 1; i <= 100; ++i) {
            const QString rel = QString("xl/worksheets/sheet%1.xml").arg(i);
            const QByteArray sheetXml = readFileFromDir(tmpDir.path(), rel);
            if (sheetXml.isEmpty()) break;
            text.append("--- Sheet").append(QString::number(i)).append(" ---\n");
            text.append(parseSheet(sheetXml, shared)).append('\n');
        }
    }

    if (text.isEmpty()) {
        r.errorMessage = "No sheets found in .xlsx";
        return r;
    }

    r.text = Utils::stripControlChars(text);
    return r;
}

} // namespace DocuSearch
