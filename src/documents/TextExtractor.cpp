// ============================================================
// TextExtractor.cpp - txt, csv, md, rtf (simple), log
// ============================================================

#include "TextExtractor.h"
#include "../core/StringUtils.h"
#include "../core/FileUtils.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

namespace DocuSearch {

QStringList TextExtractor::supportedExtensions() const {
    return {"txt", "csv", "md", "log", "rtf"};
}

ExtractionResult TextExtractor::extract(const QString& path) {
    ExtractionResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.errorMessage = "Cannot open file";
        return r;
    }
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    QString text = s.readAll();

    // RTF: very small strip - remove control words
    const QString ext = FileUtils::extensionOf(path);
    if (ext == "rtf") {
        text.remove(QRegularExpression("\\\\[a-zA-Z]+-?\\d+ ?"));
        text.remove(QRegularExpression("[{}]"));
        text.replace("\\par", "\n");
        text.replace("\\line", "\n");
        text.replace("\\tab", "\t");
        text.replace("\\\\", "\\");
        text = Utils::stripControlChars(text);
    }
    r.text = Utils::stripControlChars(text);
    r.source = "native";
    return r;
}

} // namespace DocuSearch
