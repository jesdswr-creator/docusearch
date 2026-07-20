// ============================================================
// SearchResultsHighlighter.cpp - Crash-safe search highlighting
// ============================================================

#include "SearchResultsHighlighter.h"

#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QDebug>
#include <algorithm>
#include <exception>

namespace DocuSearch {

SearchResultsHighlighter::SearchResultsHighlighter(const QStringList& searchTerms)
    : m_searchTerms(searchTerms) {
    // Filter out empty / whitespace-only terms.
    m_searchTerms.erase(
        std::remove_if(m_searchTerms.begin(), m_searchTerms.end(),
                       [](const QString& t) { return t.trimmed().isEmpty(); }),
        m_searchTerms.end());
}

int SearchResultsHighlighter::highlightText(QTextDocument* doc) {
    lastMatchCount_ = 0;

    if (!enabled_) return 0;
    if (!doc) return 0;
    if (m_searchTerms.isEmpty()) return 0;

    // Refuse to highlight documents that are too large — this is the
    // exact scenario that crashed the previous implementation.
    if (doc->characterCount() > maxDocumentChars_) {
        qWarning() << "SearchResultsHighlighter: skipping document of"
                   << doc->characterCount() << "chars (limit:"
                   << maxDocumentChars_ << ")";
        return -1;
    }

    try {
        // Build the highlight format.
        QTextCharFormat fmt;
        fmt.setBackground(highlightColor_);
        fmt.setForeground(foregroundColor_);

        // Clear any existing highlights first.
        clearHighlights(doc);

        const QTextDocument::FindFlags flags =
            caseSensitive_ ? QTextDocument::FindCaseSensitively
                           : QTextDocument::FindFlags();

        // Highlight each term, capped.
        for (const QString& term : m_searchTerms) {
            if (term.isEmpty()) continue;

            QTextCursor cursor(doc);
            cursor.beginEditBlock();  // batch — much faster than per-match updates

            int matchesThisTerm = 0;
            while (!cursor.isNull() && matchesThisTerm < maxMatchesPerTerm_) {
                cursor = doc->find(term, cursor, flags);
                if (cursor.isNull()) break;

                // Merge format (preserves existing font etc.)
                cursor.mergeCharFormat(fmt);
                ++matchesThisTerm;
                ++lastMatchCount_;

                // CRITICAL: advance cursor so we don't loop forever on
                // the same match. `find()` already moves past the match,
                // but we add an explicit safety check.
                if (!cursor.movePosition(QTextCursor::NextCharacter)) {
                    break;
                }
            }

            cursor.endEditBlock();
        }

        return lastMatchCount_;

    } catch (const std::bad_alloc& e) {
        qWarning() << "SearchResultsHighlighter: OOM —" << e.what();
        try { clearHighlights(doc); } catch (...) {}
        return -1;
    } catch (const std::exception& e) {
        qWarning() << "SearchResultsHighlighter: exception —" << e.what();
        try { clearHighlights(doc); } catch (...) {}
        return -1;
    } catch (...) {
        qWarning() << "SearchResultsHighlighter: unknown exception";
        try { clearHighlights(doc); } catch (...) {}
        return -1;
    }
}

void SearchResultsHighlighter::clearHighlights(QTextDocument* doc) {
    if (!doc) return;

    try {
        QTextCursor cursor(doc);
        cursor.select(QTextCursor::Document);

        QTextCharFormat normalFmt;
        normalFmt.setBackground(Qt::transparent);
        normalFmt.setForeground(Qt::black);
        cursor.setCharFormat(normalFmt);
    } catch (const std::exception& e) {
        qWarning() << "SearchResultsHighlighter: clearHighlights exception —" << e.what();
    } catch (...) {
        // Swallow — clearing is best-effort.
    }
}

} // namespace DocuSearch
