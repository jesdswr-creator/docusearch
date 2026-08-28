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
//
// Fidelity notes (vs. the HF BERT tokenizer reference):
//   - Accent folding: é/ü/ñ are NFD-decomposed to their base ASCII
//     letter, matching BERT's strip_accents behavior. The old code
//     DROPPED non-ASCII chars ("café" → "caf"), giving every
//     accented word a wrong embedding.
//   - Punctuation is split per character ("..." → ".", ".", ".")
//     like BERT; the old code kept whole runs as one token, which
//     usually mapped to UNK.
//   - Output length is EXACT (no padding), capped at 512 tokens.
//     The old fixed 128-token output truncated ~1000-char chunks
//     to their first half before the model ever saw them.
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

    // Task 4 Fix 1: If we hit the subword limit but there's still text
    // remaining, the word is too complex for WordPiece decomposition.
    // Return [UNK] instead of silently truncating — truncation produces
    // wrong embeddings because the model sees an incomplete word.
    if (!remaining.isEmpty()) {
        result.clear();
        result.push_back(UNK_TOKEN_ID);
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

    // 1. Lowercase (BERT-BGE style)
    QString s = text.toLower();

    // 2. Accent-fold non-ASCII letters to their ASCII base (BERT's
    //    strip_accents), normalize whitespace, and drop characters the
    //    English-only vocab cannot represent (CJK, Devanagari, …).
    //    The old code simply deleted non-ASCII, turning "café" into
    //    "caf" — a different word with a wrong embedding.
    QString filtered;
    filtered.reserve(s.size());
    for (const QChar& ch : s) {
        const ushort u = ch.unicode();
        if (u >= 32 && u <= 126) {
            filtered.append(ch);
        } else if (u == 9 || u == 10 || u == 13) {
            filtered.append(' ');
        } else if (ch.isLetter()) {
            // NFD-decompose and keep only the base character(s):
            // "é" (U+00E9) → "e" + U+0301 → keep "e".
            const QString d = QString(ch).normalized(
                QString::NormalizationForm_D);
            for (const QChar& dc : d) {
                if (dc.combiningClass() == 0) {
                    const ushort du = dc.unicode();
                    if (du >= 32 && du <= 126) {
                        filtered.append(dc);
                    }
                }
            }
        }
        // Everything else (symbols outside ASCII, digits from other
        // scripts, …) is dropped — BGE small EN is English-only.
    }
    s = filtered;

    // 3. Split into words and SINGLE punctuation characters.
    //    BERT's basic tokenizer splits every punctuation mark
    //    individually; keeping whole runs ("...", "--") produced UNK
    //    tokens because vocab.txt has no multi-char punctuation entries.
    static const QRegularExpression tokenRe(
        QStringLiteral("[A-Za-z0-9]+|[^A-Za-z0-9\\s]"));
    auto it = tokenRe.globalMatch(s);
    QStringList tokens;
    while (it.hasNext()) {
        tokens.append(it.next().captured(0));
    }

    // 4. Build token IDs via WordPiece. Reserve the exact cap so the
    //    size check below is a single comparison per subword.
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

    // 5. Cap at the model's 512-position limit. When truncating, keep
    //    [SEP] as the final token so the model sees a well-formed
    //    sequence (HF's tokenizer does the same).
    if (static_cast<int>(tokenIds.size()) > MAX_SEQ_LENGTH) {
        tokenIds.resize(MAX_SEQ_LENGTH);
        tokenIds.back() = SEP_TOKEN_ID;
    }

    // 6. Emit EXACT-LENGTH vectors (no padding). The ONNX model takes
    //    a dynamic sequence length, so short queries no longer pay for
    //    a padded 128-token inference and long chunks are no longer
    //    truncated to the first 128 tokens.
    const int n = static_cast<int>(tokenIds.size());
    out.inputIds.resize(n);
    out.attentionMask.assign(n, 1);
    out.tokenTypeIds.assign(n, 0);
    for (int i = 0; i < n; ++i) {
        out.inputIds[i] = tokenIds[i];
    }

    return out;
}

} // namespace DocuSearch
