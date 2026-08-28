// ============================================================
// DropDownCombo.h - QComboBox whose popup always opens UNDER the field
// ============================================================
// The main window is frameless (Qt::FramelessWindowHint) but re-adds
// WS_THICKFRAME for native resize; WM_NCCALCSIZE then hides the whole
// non-client area. Qt's default combo popup placement asks Windows for
// the frame metrics and gets the hidden caption/borders back, which
// shifts the computed popup position up — the list opened ON TOP of the
// field instead of dropping below it.
//
// Fix: after the base implementation sizes and shows the popup, we place
// it ourselves with plain mapToGlobal coordinates:
//   - flush under the field (1 px overlap so borders connect),
//   - width synced to the field (never narrower),
//   - height clamped to the remaining work area so a long list scrolls
//     instead of flipping over the field,
//   - near the screen bottom it opens above with the same care.
// The move happens before the popup is first painted, so there is no
// flicker. No Q_OBJECT needed: we only override a virtual method and add
// no signals/slots, keeping this header-only and moc-free.

#pragma once

#include <QAbstractItemView>
#include <QComboBox>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QWidget>

namespace DocuSearch {

class DropDownCombo : public QComboBox {
public:
    using QComboBox::QComboBox;

    void showPopup() override {
        QComboBox::showPopup();
        QWidget* pop = view()->window();
        if (!pop) return;
        const QRect work  = screen()->availableGeometry();
        const QPoint ptBelow = mapToGlobal(QPoint(-1, height() - 1));
        const QPoint ptAbove = mapToGlobal(QPoint(-1, 0));
        QSize sz = pop->size();
        sz.setWidth(qMax(width() + 2, sz.width()));
        const int roomBelow = work.bottom() - 4 - ptBelow.y();
        if (roomBelow >= qMin(sz.height(), 120)) {
            sz.setHeight(qMin(sz.height(), qMax(roomBelow, 40)));
            pop->setGeometry(QRect(ptBelow, sz));
        } else {
            const int roomAbove = ptAbove.y() - work.top() - 4;
            sz.setHeight(qMin(sz.height(), qMax(roomAbove, 40)));
            pop->setGeometry(QRect(QPoint(ptAbove.x(), ptAbove.y() - sz.height()), sz));
        }
    }
};

} // namespace DocuSearch
