// ============================================================
// StringUtils.cpp
// ============================================================

#include "StringUtils.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <algorithm>
#include <unordered_set>

namespace DocuSearch {
namespace Utils {

QString normalize(const QString& s) {
    QString out = s;
    out = out.normalized(QString::NormalizationForm_KD);
    // Strip combining marks (accents)
    out.remove(QRegularExpression("[\\p{M}]"));
    out = out.toLower();
    out.replace(QRegularExpression("\\s+"), " ");
    return out.trimmed();
}

QString toLowerAscii(const QString& s) {
    QString out = s;
    const int n = out.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = out.at(i);
        if (c.unicode() < 128) {
            if (c >= 'A' && c <= 'Z') {
                out[i] = QChar(c.unicode() + 32);
            }
        }
        // Non-ASCII chars kept as-is (CJK, etc.)
    }
    return out;
}

QString snippetAround(const QString& text, const QString& match,
                      int before, int after) {
    if (text.isEmpty()) return {};
    const int idx = text.indexOf(match, 0, Qt::CaseInsensitive);
    if (idx < 0) {
        // No match found, return the head.
        return text.left(std::min<int>(text.size(), before + after)) +
               (text.size() > before + after ? "..." : "");
    }
    int start = std::max(0, idx - before);
    int end   = std::min<int>(text.size(), idx + match.size() + after);
    QString snippet = text.mid(start, end - start);
    if (start > 0)     snippet.prepend("...");
    if (end < text.size()) snippet.append("...");
    return snippet;
}

QString fts5Quote(const QString& s) {
    // Per FTS5 docs: double quotes inside are escaped by doubling them.
    QString out = s;
    out.replace('"', "\"\"");
    return '"' + out + '"';
}

QString stripControlChars(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (const QChar& c : s) {
        const ushort u = c.unicode();
        if (u < 0x20) {
            if (u == '\n' || u == '\t' || u == '\r') out.append(c);
            // else skip
        } else {
            out.append(c);
        }
    }
    return out;
}

QString formatFileSize(qint64 bytes) {
    const double kb = 1024.0;
    if (bytes < 1024)        return QString::number(bytes) + " B";
    if (bytes < kb*1024)     return QString::number(bytes/kb, 'f', 1) + " KB";
    if (bytes < kb*kb*1024)  return QString::number(bytes/(kb*kb), 'f', 1) + " MB";
    if (bytes < kb*kb*kb*1024) return QString::number(bytes/(kb*kb*kb), 'f', 1) + " GB";
    return QString::number(bytes/(kb*kb*kb*kb), 'f', 1) + " TB";
}

ParsedQueryParts splitFieldTerms(const QString& raw) {
    ParsedQueryParts result;
    const QRegularExpression re("(\\w+):(\"[^\"]+\"|\\S+)");
    auto it = re.globalMatch(raw);
    int lastEnd = 0;
    while (it.hasNext()) {
        const auto m = it.next();
        FieldTerm ft;
        ft.field = m.captured(1).toLower();
        QString v = m.captured(2);
        if (v.startsWith('"') && v.endsWith('"') && v.size() >= 2)
            v = v.mid(1, v.size() - 2);
        ft.value = v;
        result.fields.append(ft);
        // Anything before this match that we haven't yet captured
        if (m.capturedStart() > lastEnd) {
            const QString mid = raw.mid(lastEnd, m.capturedStart() - lastEnd).trimmed();
            if (!mid.isEmpty()) result.freeTerms.append(mid);
        }
        lastEnd = m.capturedEnd();
    }
    if (lastEnd < raw.size()) {
        const QString tail = raw.mid(lastEnd).trimmed();
        if (!tail.isEmpty()) result.freeTerms.append(tail);
    }
    return result;
}

int levenshtein(const QString& a, const QString& b) {
    const int m = a.size(), n = b.size();
    if (m == 0) return n;
    if (n == 0) return m;
    std::vector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;
    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            const int cost = (a[i-1].toLower() == b[j-1].toLower()) ? 0 : 1;
            curr[j] = std::min({ prev[j] + 1, curr[j-1] + 1, prev[j-1] + cost });
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

double jaccardSimilarity(const QString& a, const QString& b) {
    auto tokenize = [](const QString& s) {
        std::unordered_set<std::wstring> set;
        for (const auto& tok : s.toLower().split(
                 QRegularExpression("[^\\w]+"), Qt::SkipEmptyParts)) {
            set.insert(tok.toStdWString());
        }
        return set;
    };
    const auto sa = tokenize(a);
    const auto sb = tokenize(b);
    if (sa.empty() && sb.empty()) return 1.0;
    if (sa.empty() || sb.empty()) return 0.0;
    int inter = 0;
    for (const auto& x : sa) if (sb.count(x)) ++inter;
    return double(inter) / double(sa.size() + sb.size() - inter);
}

QString readTextFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream s(&f);
    s.setEncoding(QStringConverter::Utf8);
    return s.readAll();
}

QString slugify(const QString& s) {
    QString out = s.toLower()
                      .normalized(QString::NormalizationForm_KD)
                      .remove(QRegularExpression("[\\p{M}]"));
    out.replace(QRegularExpression("[^a-z0-9]+"), "_");
    return out.remove(QRegularExpression("^_+|_+$"));
}

}} // namespace DocuSearch::Utils
