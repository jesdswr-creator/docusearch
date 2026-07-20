#pragma once

// ============================================================
// SearchResultsHighlighter.h - Crash-safe search term highlighting
// ============================================================
//
// Highlights search terms in the preview pane's QTextDocument.
//
// SAFETY DESIGN:
//   • All operations wrapped in try/catch (std::bad_alloc, std::exception, ...)
//   • SEH translator (installed in main.cpp) converts access violations
//     into catchable C++ exceptions.
//   • Hard cap on number of matches (prevents O(n²) slowdown on large docs)
//   • Hard cap on document size — refuses to highlight documents > N chars
//   • No infinite loops — cursor always advances past each match
//
// The previous in-PreviewPane highlighter was crashing the app on large
// documents (infinite `find()` loops + OOM). This replacement is OFF
// by default and must be explicitly enabled via setEnabled(true).
// ============================================================

#include <QString>
#include <QStringList>
#include <QColor>

class QTextDocument;

namespace DocuSearch {

class SearchResultsHighlighter {
public:
    explicit SearchResultsHighlighter(const QStringList& searchTerms = {});

    // Highlight all terms in the document. Returns the number of matches
    // actually highlighted (may be capped). Returns -1 if the document
    // was too large or an exception was caught.
    int highlightText(QTextDocument* doc);

    // Clear all highlights from a document.
    void clearHighlights(QTextDocument* doc);

    // Configuration.
    void setHighlightColor(const QColor& color) { highlightColor_ = color; }
    void setForegroundColor(const QColor& color) { foregroundColor_ = color; }
    void setCaseSensitive(bool cs)              { caseSensitive_ = cs; }
    void setEnabled(bool e)                     { enabled_ = e; }
    bool isEnabled() const                      { return enabled_; }

    // Limits (override at your own risk).
    void setMaxDocumentChars(int n)    { maxDocumentChars_ = n; }
    void setMaxMatchesPerTerm(int n)   { maxMatchesPerTerm_ = n; }

    int lastMatchCount() const { return lastMatchCount_; }

private:
    QStringList m_searchTerms;
    QColor      highlightColor_   = QColor(255, 235, 59);   // soft yellow
    QColor      foregroundColor_  = QColor(0, 0, 0);
    bool        caseSensitive_    = false;
    bool        enabled_          = false;  // OFF by default — opt-in only
    int         maxDocumentChars_ = 200000; // skip highlighting beyond this
    int         maxMatchesPerTerm_= 200;    // hard cap per term
    int         lastMatchCount_   = 0;
};

} // namespace DocuSearch
