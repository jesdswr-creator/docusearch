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
        std::vector<int64_t> inputIds;        // length 128
        std::vector<int64_t> attentionMask;   // length 128
        std::vector<int64_t> tokenTypeIds;    // length 128, all zeros
    };

    BgeTokenizer();

    TokenizerOutput encode(const QString& text);

    bool loadVocabulary(const QString& vocabPath);
    bool hasVocabulary() const { return m_hasVocab; }

    static constexpr int CLS_TOKEN_ID    = 101;
    static constexpr int SEP_TOKEN_ID    = 102;
    static constexpr int PAD_TOKEN_ID    = 0;
    static constexpr int UNK_TOKEN_ID    = 100;
    static constexpr int MAX_SEQ_LENGTH  = 128;

private:
    int hashWord(const QString& word) const;

    QHash<QString, int> m_vocab;
    bool m_hasVocab = false;
};

} // namespace DocuSearch
