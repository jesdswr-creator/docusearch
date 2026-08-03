#pragma once

// ============================================================
// ResultItemDelegate.h - Custom painter for search result items
// ============================================================
//
// Replaces the old setItemWidget approach (which created a QWidget tree
// per result row — slow for 1000+ results, no smooth hover animation).
//
// This delegate paints directly via QPainter:
//   ┌─────────────────────────────────────────────────────────────┐
//   │ ┌────┐  filename.pdf                                    ●  │
//   │ │PDF │  ...snippet text preview...                          │
//   │ └────┘  1.2 MB • 20 Aug 2025                              │
//   └─────────────────────────────────────────────────────────────┘
//
// Features:
//   - Rounded card background (transparent by default, amber-tinted on hover,
//     stronger amber-tinted on selection)
//   - 36x36 file-type badge with extension-specific color
//   - Title (bold), snippet (muted, single line), meta (smaller, more muted)
//   - Smooth 150ms hover transition via QPropertyAnimation on the hoverProgress
//   - Score chip on the right (if hit has a score > 0)
//
// All colors come from the current QPalette so the delegate works in both
// light and dark themes without hard-coded colors.
// ============================================================

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>

namespace DocuSearch {

class ResultItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ResultItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

private:
    // Color for the file-type badge, based on extension.
    QColor badgeColor(const QString& ext) const;
    QString badgeLabel(const QString& ext) const;
    QString humanizeSize(qint64 bytes) const;
    QString stripBoldTags(const QString& s) const;
};

} // namespace DocuSearch
