// ============================================================
// TextQuality.cpp - Extracted-text sanity classification
// ============================================================
//
// Detection gates (any single one flags the text as garbage):
//
//  Gate A -- PUA / replacement-soup: >2% of non-space characters are
//     Private Use Area (U+E000-F8FF), replacement (U+FFFD) or C1
//     controls (U+007F-009F). Real prose contains ~none of these;
//     PDFs with PUA-mapping ToUnicode CMaps are nearly 100% PUA.
//
//  Gate B -- Latin word-sanity: for text that is >=85% Latin letters,
//     ALL of the following must hold to flag garbage:
//       - common-word rate < 2%  (against a built-in stopword list
//         covering English + major European languages, so real
//         French/German/Spanish... documents never trip it)
//       - vowel-less ratio > 45% among words of >=4 letters
//         (converter garbage like "xwj qvxqz" is almost entirely
//         vowel-less; real language never is)
//       - >=25 alphabetic tokens (short snippets carry too little
//         signal - never flagged)
//
// Everything else - CJK, Devanagari, Arabic, Cyrillic, mixed
// scripts, numeric tables, code listings - passes untouched.

#include "TextQuality.h"

#include <QSet>
#include <QStringList>

namespace DocuSearch::TextQuality {

namespace {

// ---- tuning constants (validated against a broken-PDF corpus) ----
constexpr int      kMinSampleChars   = 20;      // below: no opinion
constexpr int      kMaxSampleChars   = 20000;   // analysis bound
constexpr int      kMinTokens        = 25;      // word gate needs signal
constexpr double   kWeirdRatio       = 0.02;    // gate A threshold
constexpr double   kMinLetterRatio   = 0.55;    // of non-space chars
constexpr double   kMinAsciiShare    = 0.85;    // of letters, gate B scope
constexpr double   kMaxCommonRate    = 0.02;    // gate B: common-word rate
constexpr double   kMinVowellessRate = 0.45;    // gate B: vowel-less ratio
constexpr int      kMinVowellessLen  = 4;       // word length for that ratio

bool isWeirdChar(uint u) {
    return (u >= 0xE000 && u <= 0xF8FF) ||  // Private Use Area
           u == 0xFFFD || u == 0xFFFE || u == 0xFFFF ||
           (u >= 0x007F && u <= 0x009F);    // C1 controls
}

bool isAlphaChar(uint u) {
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
           (u >= 0x00C0 && u <= 0x024F);    // Latin-1 letters + Extended-A/B
}

bool isAnyLetter(uint u) {
    // Letter check generous enough for the ratio pre-filter; covers
    // Latin, Greek, Cyrillic, CJK, Devanagari, Arabic, Hebrew, kana...
    return (u >= 0x0041 && u <= 0x005A) || (u >= 0x0061 && u <= 0x007A) ||
           (u >= 0x00AA && u <= 0x00AA) ||
           (u >= 0x00B5 && u <= 0x00B5) ||
           (u >= 0x00BA && u <= 0x00BA) ||
           (u >= 0x00C0 && u <= 0x02AF) ||   // Latin extended
           (u >= 0x0370 && u <= 0x04FF) ||   // Greek + Cyrillic
           (u >= 0x0590 && u <= 0x05FF) ||   // Hebrew
           (u >= 0x0600 && u <= 0x06FF) ||   // Arabic
           (u >= 0x0900 && u <= 0x097F) ||   // Devanagari
           (u >= 0x0E00 && u <= 0x0E7F) ||   // Thai
           (u >= 0x3040 && u <= 0x30FF) ||   // Hiragana/Katakana
           (u >= 0x3400 && u <= 0x4DBF) ||   // CJK ext A
           (u >= 0x4E00 && u <= 0x9FFF) ||   // CJK
           (u >= 0xAC00 && u <= 0xD7AF);     // Hangul
}

bool hasVowel(const QString& word) {
    static const QSet<ushort> kVowels = {
        'a','e','i','o','u','y','A','E','I','O','U','Y',
        0x00E0,0x00E1,0x00E2,0x00E3,0x00E4,0x00E5,       // à-å
        0x00E8,0x00E9,0x00EA,0x00EB,                     // è-ë
        0x00EC,0x00ED,0x00EE,0x00EF,                     // ì-ï
        0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,              // ò-ö
        0x00F9,0x00FA,0x00FB,0x00FC,0x00FD,0x00FF,       // ù-ü,ý,ÿ
        0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,
        0x00C8,0x00C9,0x00CA,0x00CB,
        0x00CC,0x00CD,0x00CE,0x00CF,
        0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,
        0x00D9,0x00DA,0x00DB,0x00DC,0x00DD
    };
    for (const QChar c : word)
        if (kVowels.contains(c.unicode())) return true;
    return false;
}

// Common words of English + the major European Latin-script languages.
// Purpose: keep REAL documents in those languages above the common-word
// threshold. Garbage (broken CMap decoding) contains none of these.
QSet<QString> commonWords() {
    static const QSet<QString> kWords = [] {
        const QStringList words = QStringList{
        // English (top ~120)
        "the","and","of","to","in","for","is","on","that","with","as","was",
        "at","by","an","be","or","from","this","it","are","have","not",
        "which","but","all","can","has","were","said","one","their","what",
        "when","will","more","other","than","its","out","about","who","get",
        "also","if","into","no","only","our","you","your","we","they","them",
        "he","she","his","her","him","do","does","did","done","just","now",
        "over","such","most","may","each","any","some","there","here",
        "where","why","how","because","between","during","before","after",
        "above","below","up","down","off","then","once","under","again",
        "both","few","own","same","so","too","very","shall","been","being",
        "would","could","should","must","these","those","upon","while",
        "through","against","within","without","among","per","via","etc",
        // German
        "und","der","die","das","ist","nicht","mit","den","von","zu","auch",
        "auf","für","im","ein","eine","einen","dem","des","dass","sind",
        "haben","wird","werden","oder","aber","wie","sich","bei","aus",
        // French
        "le","la","les","et","une","est","dans","pour","que","qui","sur",
        "avec","pas","plus","par","ce","cette","ses","sont","leur","nous",
        "vous","ils","elles","mais","où","au","aux","d'un","d'une","été",
        // Spanish
        "el","en","un","una","los","las","del","se","con","por","para","su",
        "sus","como","más","pero","antes","después","entre","cuando",
        "porque","este","esta","estos","estas","muy","ya","fue","son","dos",
        // Italian
        "di","che","per","della","sono","essere","non","più","anche","come",
        "da","nel","alla","degli","delle",
        // Portuguese
        "de","o","em","uma","não","dos","das","seu","sua","isso","esse",
        "essa","pelo","pela",
        // Dutch
        "het","een","van","zijn","niet","met","voor","op","aan","er","maar",
        "ook","worden","werd",
        // Turkish
        "ve","bu","ile","için","olarak","daha","çok","ama","gibi","bir",
        };
        return QSet<QString>(words.cbegin(), words.cend());
    }();
    return kWords;
}

} // namespace

bool looksLikeGarbage(const QString& text, QString* reason) {
    if (reason) reason->clear();
    if (text.isEmpty()) return false;

    const QString sample = text.left(kMaxSampleChars);

    // ---- character-level stats over non-space chars ----
    int n = 0, weird = 0, letters = 0;
    for (const QChar c : sample) {
        if (c.isSpace()) continue;
        const uint u = c.unicode();
        ++n;
        if (isWeirdChar(u)) ++weird;
        if (isAnyLetter(u)) ++letters;
    }
    if (n < kMinSampleChars) return false;          // too little signal

    // ---- gate A: PUA / replacement / C1 soup ----
    if (weird > 0) {
        const double ratio = double(weird) / double(n);
        if (ratio > kWeirdRatio) {
            if (reason) *reason = QString("pua/c1 ratio %1")
                                      .arg(ratio, 0, 'f', 3);
            return true;
        }
    }

    // ---- gate B scope: Latin-dominant text only ----
    if (letters < kMinLetterRatio * n) return false;

    // ---- tokenize: runs of Latin letters ----
    QStringList tokens;
    QString cur;
    cur.reserve(16);
    int asciiLetters = 0;
    for (const QChar c : sample) {
        const uint u = c.unicode();
        if (isAlphaChar(u)) {
            cur.append(c);
            if (u < 0x0250) ++asciiLetters;
        } else if (!cur.isEmpty()) {
            tokens.append(cur);
            cur.clear();
        }
    }
    if (!cur.isEmpty()) tokens.append(cur);

    if (letters > 0 &&
        double(asciiLetters) / double(letters) < kMinAsciiShare) {
        return false;   // significant non-Latin script - never word-gated
    }
    if (tokens.size() < kMinTokens) return false;   // not enough signal

    // ---- gate B evidence 1: common-word rate ----
    const QSet<QString> common = commonWords();
    int hits = 0;
    for (const QString& t : tokens) {
        if (common.contains(t.toLower())) ++hits;
    }
    const double commonRate = double(hits) / double(tokens.size());

    // ---- gate B evidence 2: vowel-less ratio (words >= 4 letters) ----
    int longWords = 0, vowelless = 0;
    for (const QString& t : tokens) {
        if (t.size() >= kMinVowellessLen) {
            ++longWords;
            if (!hasVowel(t)) ++vowelless;
        }
    }
    const double vowellessRate = longWords > 0
        ? double(vowelless) / double(longWords) : 0.0;

    if (commonRate < kMaxCommonRate && vowellessRate > kMinVowellessRate) {
        if (reason) *reason = QString("word-sanity: common %1, vowelless %2")
                                  .arg(commonRate, 0, 'f', 3)
                                  .arg(vowellessRate, 0, 'f', 3);
        return true;
    }
    return false;
}

} // namespace DocuSearch::TextQuality
