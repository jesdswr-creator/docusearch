// ============================================================
// TitleBarWidget.cpp — implementation
// ============================================================

#include "TitleBarWidget.h"

#include <QMouseEvent>
#include <QWindow>

namespace DocuSearch {

TitleBarWidget::TitleBarWidget(QWidget* windowOwner, QWidget* parent)
    : QWidget(parent), windowOwner_(windowOwner) {
    setFixedHeight(44);
    setObjectName("titleBar");
    setMouseTracking(true);
}

void TitleBarWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && windowOwner_) {
        // Delegate to the platform's native move loop instead of manual
        // owner_->move() tracking. The manual tracker had a fatal flaw:
        // if the release happened outside the window (Alt+Tab, a toast
        // stealing focus, Win+Down minimizing mid-drag), dragging_ stayed
        // true forever and every later mouse-move teleported the window —
        // which also wedged WM_NCHITTEST resize handling until restart.
        // startSystemMove() hands control to Windows, supports Aero Snap,
        // and can never get stuck in a half-finished drag.
        if (windowOwner_->windowHandle())
            windowOwner_->windowHandle()->startSystemMove();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TitleBarWidget::mouseMoveEvent(QMouseEvent* e) {
    QWidget::mouseMoveEvent(e);  // native loop owns movement
}

void TitleBarWidget::mouseReleaseEvent(QMouseEvent* e) {
    QWidget::mouseReleaseEvent(e);
}

void TitleBarWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && windowOwner_) {
        if (windowOwner_->isMaximized()) {
            windowOwner_->showNormal();
        } else {
            windowOwner_->showMaximized();
        }
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

} // namespace DocuSearch
