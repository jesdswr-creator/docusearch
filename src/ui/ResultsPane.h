#pragma once

// ============================================================
// ResultsPane.h - Search results list matching reference design
// ============================================================
//
// Layout:
//   ┌─────────────────────────────────────────────────┐
//   │ Search Results (36)              [Sort: ... ▾]  │  header
//   ├─────────────────────────────────────────────────┤
//   │ [PDF] NOC.pdf                                  ●│  item
//   │       ...Request for issuance No Objection...   │
//   │       57.3 KB • 20 Aug 2025                     │
//   ├─────────────────────────────────────────────────┤
//   │ [PDF] Additional format.pdf                     │
//   │       ...No Statutory clearance Details...      │
//   │       1.2 MB • 09 Sep 2025                      │
//   └─────────────────────────────────────────────────┘
//
// Each item is a custom widget with:
//   - 36x36 colored file-type badge (PDF red, DOC blue, XLS green, etc.)
//   - Title (bold), snippet (muted), meta (smaller, more muted)
//   - Optional blue dot on the right for the active item
// ============================================================

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QComboBox>
#include <QList>
#include "../core/Types.h"

class QPushButton;

namespace DocuSearch {

class ResultsPane : public QWidget {
    Q_OBJECT
public:
    explicit ResultsPane(QWidget* parent = nullptr);

    void setResults(const QList<SearchHit>& hits);
    void appendResults(const QList<SearchHit>& hits);
    void clear();

    // Persistent one-line summary of how AI shaped the CURRENT result set
    // (e.g. "AI contributed to 7 of 21 results - 3 found by AI alone").
    // Shown as a tinted pill under the header; hidden when text is empty.
    // This makes the AI contribution visible without digging through the
    // transient status-bar toast.
    void setAiSummary(const QString& text);

    // v1.7.13: optional header action button (e.g. "Delete duplicate
    // copies"). Hidden unless armed with a non-empty label; setResults()
    // and clear() always disarm it, so a stale action can never attach
    // to the wrong result set.
    void setAction(const QString& label);

    qint64 selectedFileId() const;
    QString selectedPath() const;

    void refreshIcons();

signals:
    void fileSelected(qint64 fileId, const QString& path);
    void fileActivated(qint64 fileId, const QString& path);
    void actionRequested();

private slots:
    void onItemClicked(int row);
    void onItemDoubleClicked(int row);
    void onSortChanged(int index);

private:
    QListWidget* list_       = nullptr;
    QLabel*      titleLbl_   = nullptr;
    QLabel*      countLbl_   = nullptr;
    QLabel*      aiSummaryLbl_ = nullptr;
    QComboBox*   sortBox_    = nullptr;
    QPushButton* actionBtn_  = nullptr;
    QList<SearchHit> current_;

    void populateItem(int row, const SearchHit& h);
    QString colorForExtension(const QString& ext) const;
    QString humanizeSize(qint64 bytes) const;
    QString stripBoldTags(const QString& s) const;
    QString labelForExtension(const QString& ext) const;
};

} // namespace DocuSearch
