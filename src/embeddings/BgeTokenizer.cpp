// ============================================================
// BgeTokenizer.cpp - BERT WordPiece tokenizer for BGE model
// ============================================================
//
// CRITICAL: A vocab.txt file MUST be loaded before encode() can
// produce meaningful output. Without the vocab, encode() returns
// an empty TokenizerOutput (inputIds empty) — callers MUST check
// inputIds.empty() and skip semantic scoring in that case.
//
// The previous hash-based fallback was fundamentally broken: it
// mapped words to random token IDs, producing semantically
// meaningless embeddings. See HIGH-1 in the review report.
// ============================================================

#include "BgeTokenizer.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QtGlobal>
#include <cmath>
#include <algorithm>

namespace DocuSearch {

BgeTokenizer::BgeTokenizer() = default;

bool BgeTokenizer::loadVocabulary(const QString& vocabPath) {
    QFile f(vocabPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_hasVocab = false;
        return false;
    }
    m_vocab.clear();
    QTextStream in(&f);
    int idx = 0;
    while (!in.atEnd()) {
        // vocab.txt format: one token per line, line number = token ID.
        // Lines may have trailing whitespace or comments after tabs.
        QString line = in.readLine();
        // Strip trailing whitespace and tab-separated comments.
        int tabPos = line.indexOf('\t');
        if (tabPos >= 0) line = line.left(tabPos);
        line = line.trimmed();
        // Even empty lines count as a token ID slot (BERT vocab has [unused] tokens).
        m_vocab.insert(line, idx);
        ++idx;
    }
    f.close();
    m_hasVocab = !m_vocab.isEmpty();
    return m_hasVocab;
}

// WordPiece segmentation: greedy longest-match from the start.
// Tries the whole word first, then progressively strips a character
// from the front, prepending "##" to mark continuation tokens.
// Returns UNK_TOKEN_ID if no subword decomposition is found.
std::vector<int> BgeTokenizer::wordpieceTokenize(const QString& word) {
    std::vector<int> result;
    if (word.isEmpty() || !m_hasVocab) return result;

    const int maxSubwords = 8;  // cap per word to avoid runaway decomposition
    QString remaining = word;
    int subwordCount = 0;
    bool isFirst = true;

    while (!remaining.isEmpty() && subwordCount < maxSubwords) {
        int end = remaining.length();
        int matchLen = 0;
        int matchId = -1;

        // Greedy longest-match: try the whole remaining string, then
        // progressively shorter prefixes.
        for (int len = end; len > 0; --len) {
            QString candidate = remaining.left(len);
            if (!isFirst) {
                candidate = QStringLiteral("##") + candidate;
            }
            auto it = m_vocab.constFind(candidate);
            if (it != m_vocab.constEnd()) {
                matchLen = len;
                matchId = it.value();
                break;
            }
        }

        if (matchLen == 0) {
            // No subword match — entire word is UNK.
            result.clear();
            result.push_back(UNK_TOKEN_ID);
            return result;
        }

        result.push_back(matchId);
        remaining = remaining.mid(matchLen);
        isFirst = false;
        ++subwordCount;
    }
    return result;
}

BgeTokenizer::TokenizerOutput BgeTokenizer::encode(const QString& text) {
    TokenizerOutput out;

    // CRITICAL: If vocab not loaded, return empty output.
    // Caller MUST check inputIds.empty() and skip semantic scoring.
    if (!m_hasVocab) {
        return out;  // empty TokenizerOutput
    }

    out.inputIds.assign(MAX_SEQ_LENGTH, PAD_TOKEN_ID);
    out.attentionMask.assign(MAX_SEQ_LENGTH, 0);
    out.tokenTypeIds.assign(MAX_SEQ_LENGTH, 0);

    // 1. Lowercase (BERT-BGE style)
    QString s = text.toLower();

    // 2. Remove non-printable ASCII (keep 32..126), normalize whitespace
    QString filtered;
    filtered.reserve(s.size());
    for (const QChar& ch : s) {
        const ushort u = ch.unicode();
        if (u >= 32 && u <= 126) {
            filtered.append(ch);
        } else if (u == 9 || u == 10 || u == 13) {
            filtered.append(' ');
        }
        // Non-ASCII characters are dropped — BGE small EN is English-only.
    }
    s = filtered;

    // 3. Split on whitespace and punctuation.
    // BERT uses a regex like: \w+|[^\w\s]+  (words OR punctuation runs).
    static const QRegularExpression tokenRe(
        QStringLiteral("[A-Za-z0-9]+|[^A-Za-z0-9\\s]+"));
    auto it = tokenRe.globalMatch(s);
    QStringList tokens;
    while (it.hasNext()) {
        tokens.append(it.next().captured(0));
    }

    // 4. Build token IDs via WordPiece.
    std::vector<int> tokenIds;
    tokenIds.reserve(MAX_SEQ_LENGTH);
    tokenIds.push_back(CLS_TOKEN_ID);

    for (const QString& w : tokens) {
        if (static_cast<int>(tokenIds.size()) >= MAX_SEQ_LENGTH - 1) break;
        const auto subwords = wordpieceTokenize(w);
        for (int swId : subwords) {
            if (static_cast<int>(tokenIds.size()) >= MAX_SEQ_LENGTH - 1) break;
            tokenIds.push_back(swId);
        }
    }
    tokenIds.push_back(SEP_TOKEN_ID);

    // 5. Copy into output (capped at MAX_SEQ_LENGTH).
    const int n = std::min(static_cast<int>(tokenIds.size()), MAX_SEQ_LENGTH);
    for (int i = 0; i < n; ++i) {
        out.inputIds[i] = tokenIds[i];
        out.attentionMask[i] = 1;
    }

    return out;
}

} // namespace DocuSearch
