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
bool looksLikeGarbage(const QString& text, QString* reason = nullptr);

} // namespace DocuSearch::TextQuality
