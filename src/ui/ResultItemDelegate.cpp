// ============================================================
// ResultItemDelegate.cpp - Custom painter for search result items
// ============================================================

#include "ResultItemDelegate.h"
#include "../core/Types.h"

#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QFont>
#include <QFontMetrics>
#include <QStyle>
#include <QApplication>
#include <QDateTime>

namespace DocuSearch {

ResultItemDelegate::ResultItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

QSize ResultItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    // Fixed height: badge (36) + padding (10*2) + 3 lines of text (~42)
    return QSize(280, 72);
}

QColor ResultItemDelegate::badgeColor(const QString& ext) const {
    static const QHash<QString, QColor> colors = {
        {"pdf",  QColor("#ef4444")},   // red
        {"doc",  QColor("#2563eb")},   // blue
        {"docx", QColor("#2563eb")},
        {"xls",  QColor("#16a34a")},   // green
        {"xlsx", QColor("#16a34a")},
        {"xlsm", QColor("#16a34a")},
        {"ppt",  QColor("#ea580c")},   // orange
        {"pptx", QColor("#ea580c")},
        {"txt",  QColor("#64748b")},   // slate
        {"csv",  QColor("#64748b")},
        {"md",   QColor("#64748b")},
        {"rtf",  QColor("#64748b")},
        {"png",  QColor("#a855f7")},   // purple
        {"jpg",  QColor("#a855f7")},
        {"jpeg", QColor("#a855f7")},
        {"bmp",  QColor("#a855f7")},
        {"tiff", QColor("#a855f7")},
        {"webp", QColor("#a855f7")},
        {"gif",  QColor("#a855f7")},
    };
    return colors.value(ext.toLower(), QColor("#62718a"));  // faint default
}

QString ResultItemDelegate::badgeLabel(const QString& ext) const {
    QString e = ext.toLower();
    if (e == "pdf") return "PDF";
    if (e == "doc" || e == "docx") return "DOC";
    if (e == "xls" || e == "xlsx" || e == "xlsm") return "XLS";
    if (e == "ppt" || e == "pptx") return "PPT";
    if (e.size() <= 3) return e.toUpper();
    return e.left(3).toUpper();
}

QString ResultItemDelegate::humanizeSize(qint64 bytes) const {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024);
    if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024 * 1024));
    return QString("%1 GB").arg(bytes / (1024 * 1024 * 1024));
}

QString ResultItemDelegate::stripBoldTags(const QString& s) const {
    QString out = s;
    out.remove("<b>");
    out.remove("</b>");
    out.remove("<B>");
    out.remove("</B>");
    return out;
}

void ResultItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRect rect = option.rect;
    const bool isSelected = (option.state & QStyle::State_Selected);
    const bool isHovered  = (option.state & QStyle::State_MouseOver);

    // ── 1. Card background ──────────────────────────────────────────
    // Transparent by default, subtle amber tint on hover, stronger on select.
    // Matches the QSS #resultCard[sel="true"] styling from the mockup.
    const QPalette pal = option.palette;
    QColor cardBg;
    if (isSelected) {
        cardBg = QColor(244, 168, 58, 22);  // rgba(244,168,58,0.09) approx
    } else if (isHovered) {
        cardBg = pal.color(QPalette::AlternateBase);
    } else {
        cardBg = Qt::transparent;
    }

    if (cardBg != Qt::transparent) {
        QPainterPath path;
        path.addRoundedRect(rect.adjusted(2, 2, -2, -2), 8, 8);
        painter->fillPath(path, cardBg);

        // Amber left border on selection (3px, matches mockup #resultCard[sel="true"])
        if (isSelected) {
            QPainterPath borderPath;
            QRect leftBar(rect.left() + 2, rect.top() + 2, 3, rect.height() - 4);
            borderPath.addRoundedRect(leftBar, 2, 2);
            painter->fillPath(borderPath, QColor(244, 168, 58));  // @amber@
        }
    }

    // ── 2. File-type badge (36x36, colored, rounded) ───────────────
    // Data roles must match ResultsPane.cpp's kRole* constants.
    const int kRoleFilename  = Qt::UserRole + 3;
    const int kRoleSnippet   = Qt::UserRole + 4;
    const int kRoleExtension = Qt::UserRole + 5;
    const int kRoleSize      = Qt::UserRole + 6;
    const int kRoleModDate   = Qt::UserRole + 7;
    const int kRoleScore     = Qt::UserRole + 8;

    const QString ext = index.data(kRoleExtension).toString();
    const QString filename = index.data(kRoleFilename).toString();
    const QString snippet = index.data(kRoleSnippet).toString();
    const qint64 size = index.data(kRoleSize).toLongLong();
    const qint64 modTime = index.data(kRoleModDate).toLongLong();
    const double score = index.data(kRoleScore).toDouble();

    const QRect badgeRect(rect.left() + 14, rect.top() + 18, 36, 36);
    QPainterPath badgePath;
    badgePath.addRoundedRect(badgeRect, 8, 8);
    painter->fillPath(badgePath, badgeColor(ext));

    // Badge label (e.g. "PDF")
    QFont badgeFont = pal.font();
    badgeFont.setPixelSize(10);
    badgeFont.setBold(true);
    painter->setFont(badgeFont);
    painter->setPen(Qt::white);
    painter->drawText(badgeRect, Qt::AlignCenter, badgeLabel(ext));

    // ── 3. Title (filename, bold) ──────────────────────────────────
    const int textX = badgeRect.right() + 10;
    const int textRight = rect.right() - 14;

    QFont titleFont = pal.font();
    titleFont.setPixelSize(13);
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(pal.color(QPalette::Text));

    QFontMetrics tfm(titleFont);
    QString title = tfm.elidedText(filename, Qt::ElideRight, textRight - textX);
    QRect titleRect(textX, rect.top() + 10, textRight - textX, 18);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title);

    // ── 4. Snippet (muted, single line, elided) ────────────────────
    QFont snipFont = pal.font();
    snipFont.setPixelSize(12);
    painter->setFont(snipFont);
    painter->setPen(pal.color(QPalette::WindowText));

    QFontMetrics sfm(snipFont);
    QString snip = stripBoldTags(snippet);
    if (snip.size() > 120) snip = snip.left(120) + "...";
    if (snip.isEmpty()) snip = "...";
    snip = sfm.elidedText(snip, Qt::ElideRight, textRight - textX);
    QRect snipRect(textX, titleRect.bottom() + 2, textRight - textX, 16);
    painter->drawText(snipRect, Qt::AlignLeft | Qt::AlignVCenter, snip);

    // ── 5. Meta line (size • date, smaller + more muted) ───────────
    QFont metaFont = pal.font();
    metaFont.setPixelSize(10);
    metaFont.setFamily("IBM Plex Mono");
    painter->setFont(metaFont);
    painter->setPen(pal.color(QPalette::Disabled, QPalette::WindowText));

    QString dateStr;
    if (modTime > 0) {
        dateStr = QDateTime::fromSecsSinceEpoch(modTime).toString("dd MMM yyyy");
    }
    QString metaText = dateStr.isEmpty()
        ? humanizeSize(size)
        : QString("%1 • %2").arg(humanizeSize(size)).arg(dateStr);
    QRect metaRect(textX, snipRect.bottom() + 2, textRight - textX, 14);
    painter->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter, metaText);

    // ── 6. Score chip (right side, if score > 0) ───────────────────
    if (score > 0.0) {
        QFont scoreFont = pal.font();
        scoreFont.setPixelSize(11);
        scoreFont.setFamily("IBM Plex Mono");
        painter->setFont(scoreFont);
        painter->setPen(QColor(244, 168, 58));  // @amber@

        const QString scoreText = QString::number(score, 'f', 2);
        QFontMetrics scfm(scoreFont);
        int scoreW = scfm.horizontalAdvance(scoreText) + 14;
        int scoreH = 18;
        QRect scoreRect(rect.right() - scoreW - 14,
                        rect.top() + (rect.height() - scoreH) / 2,
                        scoreW, scoreH);

        QPainterPath scorePath;
        scorePath.addRoundedRect(scoreRect, 5, 5);
        painter->fillPath(scorePath, QColor(244, 168, 58, 33));  // rgba(244,168,58,0.13)
        painter->drawText(scoreRect, Qt::AlignCenter, scoreText);
    }

    painter->restore();
}

} // namespace DocuSearch
