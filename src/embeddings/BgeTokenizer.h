#pragma once

// ============================================================
// BgeTokenizer.h - Tokenizer for BGE Small EN v1.5 model
// ============================================================

#include <QString>
#include <QHash>
#include <vector>

namespace DocuSearch {

class BgeTokenizer {
public:
    struct TokenizerOutput {
        // EXACT-LENGTH vectors (no padding): always size n where
        // 2 <= n <= MAX_SEQ_LENGTH. inputIds starts with CLS and ends
        // with SEP; attentionMask is all 1s; tokenTypeIds all 0s.
        // Embedding at exact length removes the old fixed-128 padding,
        // which truncated every chunk longer than ~128 tokens (roughly
        // the first 500 characters) and padded short queries out to 128
        // (4-8x slower query inference than needed).
        std::vector<int64_t> inputIds;        // length n (exact)
        std::vector<int64_t> attentionMask;   // length n, all 1
        std::vector<int64_t> tokenTypeIds;    // length n, all 0
    };

    BgeTokenizer();

    TokenizerOutput encode(const QString& text);

    bool loadVocabulary(const QString& vocabPath);
    bool hasVocabulary() const { return m_hasVocab; }

    static constexpr int CLS_TOKEN_ID    = 101;
    static constexpr int SEP_TOKEN_ID    = 102;
    static constexpr int PAD_TOKEN_ID    = 0;
    static constexpr int UNK_TOKEN_ID    = 100;
    // BGE Small EN v1.5 supports 512-position embeddings. The old cap
    // of 128 silently truncated chunks (~1000 chars ≈ 250-350 tokens)
    // to their first half before inference.
    static constexpr int MAX_SEQ_LENGTH  = 512;

private:
    // WordPiece segmentation: greedy longest-match from the start.
    // Returns a vector of token IDs for the given word. If no decomposition
    // is found, returns a single UNK_TOKEN_ID. If vocab not loaded,
    // returns an empty vector.
    std::vector<int> wordpieceTokenize(const QString& word);

    QHash<QString, int> m_vocab;
    bool m_hasVocab = false;
};

} // namespace DocuSearch
