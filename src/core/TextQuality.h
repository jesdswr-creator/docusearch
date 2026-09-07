// ============================================================
// TextQuality.h - Extracted-text sanity classification
// ============================================================
//
// v1.7.2: some PDFs carry a text layer that DECODES TO GIBBERISH.
// The page renders perfectly (real glyphs drawn from the embedded
// font), but the mapping from character codes to Unicode that the
// extractor relies on is broken - the classic generators are cheap
// PDF converters and legacy DTP / legacy-font workflows that write a
// bogus or missing /ToUnicode CMap. The extractor then returns
// "meaningless alphabet" (e.g. "xwj qvxqz xzwxv kwq") or Private Use
// Area soup, which DocuSearch would index as if it were real text.
//
// looksLikeGarbage() identifies such text CONFIDENTLY so the PDF
// extractor can discard it and route the file to OCR (the rendered
// page still shows the real glyphs, so OCR recovers readable text).
//
// Conservative by design: every gate requires strong, redundant
// evidence. False positives (flagging real text) are far more
// expensive than false negatives (letting some garbage through).

#pragma once

#include <QString>

namespace DocuSearch::TextQuality {

// True when `text` is confidently machine-garbled.
// `reason` (optional) receives a short human-readable explanation
// suitable for log lines, e.g. "pua/c1 ratio 0.985".
//
// v1.7.16 adds gate C (scanner/fragment soup): text layers that OCR
// software embedded INTO a PDF at scan time (Canon IJ Scan Utility and
// friends) can be garbage that gates A/B never see - punctuation-heavy
// letter fragments ("AVAA'IIVH NHAISAAA HINOS ;; i g !c ...") from a
// rotated page. Indexing it poisons both keyword and AI search while
// the real text is only recoverable by OCR-ing the rendered page.
bool looksLikeGarbage(const QString& text, QString* reason = nullptr);

// v1.7.16: quality metrics for OCR OUTPUT, used by the auto-orientation
// logic to decide whether the upright pass read real text or sideways
// fragments, and which rotation candidate to keep.
struct OcrTextStats {
    int    runScore      = 0;    // v1.7.10 metric: chars in runs of >=3 alnum
    int    tokens        = 0;    // Latin tokens of >=2 letters
    int    dictHits      = 0;    // tokens found in the common-word list
    double wordRate      = 0.0;  // dictHits / tokens (0 when tokens == 0)
    bool   latinDominant = false;// false for CJK/Devanagari/... - wordRate
                                 // is only meaningful when true
};
OcrTextStats assessOcrText(const QString& text);

} // namespace DocuSearch::TextQuality
