// ============================================================
// BgeTokenizer.cpp - Hash-based tokenizer for BGE model
// ============================================================

#include "BgeTokenizer.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QtGlobal>
#include <cmath>

namespace DocuSearch {

BgeTokenizer::BgeTokenizer() = default;

int BgeTokenizer::hashWord(const QString& word) const {
    // Maps any word to a token ID in range [1000, 30521].
    // Uses Qt's qHash for a stable, well-distributed hash.
    return (int)(qHash(word) % 29522) + 1000;
}

bool BgeTokenizer::loadVocabulary(const QString& vocabPath) {
    QFile f(vocabPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&f);
    int idx = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) { ++idx; continue; }
        m_vocab.insert(line, idx);
        ++idx;
    }
    f.close();
    m_hasVocab = !m_vocab.isEmpty();
    return m_hasVocab;
}

BgeTokenizer::TokenizerOutput BgeTokenizer::encode(const QString& text) {
    TokenizerOutput out;
    out.inputIds.assign(MAX_SEQ_LENGTH, PAD_TOKEN_ID);
    out.attentionMask.assign(MAX_SEQ_LENGTH, 0);
    out.tokenTypeIds.assign(MAX_SEQ_LENGTH, 0);

    // 1. Lowercase
    QString s = text.toLower();

    // 2. Remove non-ASCII (keep only ASCII 32..122)
    QString filtered;
    filtered.reserve(s.size());
    for (const QChar& ch : s) {
        const ushort u = ch.unicode();
        if (u >= 32 && u <= 122) {
            filtered.append(ch);
        } else if (u == 9 || u == 10 || u == 13) {
            filtered.append(' ');  // tab/newline → space
        }
    }
    s = filtered;

    // 3. Replace punctuation with spaces
    static const QRegularExpression punctRe(
        QStringLiteral("[!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~]"));
    s.replace(punctRe, QStringLiteral(" "));

    // 4. Split on whitespace
    const QStringList words = s.split(' ', Qt::SkipEmptyParts);

    // 5. Build token IDs
    std::vector<int> tokens;
    tokens.reserve(MAX_SEQ_LENGTH);
    tokens.push_back(CLS_TOKEN_ID);

    for (const QString& w : words) {
        if (w.isEmpty()) continue;
        if (static_cast<int>(tokens.size()) >= MAX_SEQ_LENGTH - 1) break;
        int tokenId;
        if (m_hasVocab) {
            auto it = m_vocab.constFind(w);
            tokenId = (it != m_vocab.constEnd()) ? it.value() : UNK_TOKEN_ID;
        } else {
            tokenId = hashWord(w);
        }
        tokens.push_back(tokenId);
    }
    tokens.push_back(SEP_TOKEN_ID);

    // 6. Copy into output (capped at MAX_SEQ_LENGTH)
    const int n = std::min(static_cast<int>(tokens.size()), MAX_SEQ_LENGTH);
    for (int i = 0; i < n; ++i) {
        out.inputIds[i] = tokens[i];
        out.attentionMask[i] = 1;
    }

    return out;
}

} // namespace DocuSearch
