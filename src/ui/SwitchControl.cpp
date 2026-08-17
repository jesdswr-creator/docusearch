// ============================================================
// SwitchControl.cpp - iOS-style pill toggle, QSS-safe and palette-aware
// ============================================================

#include "SwitchControl.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QGuiApplication>
#include <QPalette>
#include <QApplication>

namespace DocuSearch {

// Match the Pastel Pop design:
//   .switch { width:36px; height:21px; border-radius:999px }
//   .switch::after { width:15px; height:15px; top:3px; left:3px → 18px }
static constexpr int kTrackW = 36;
static constexpr int kTrackH = 21;
static constexpr int kKnobD  = 15;
static constexpr int kPad    = 3;

SwitchControl::SwitchControl(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    anim_ = new QPropertyAnimation(this, "knobPos", this);
    anim_->setDuration(160);
    anim_->setEasingCurve(QEasingCurve::OutCubic);

    connect(this, &QAbstractButton::toggled, this, &SwitchControl::onToggled);
}

SwitchControl::~SwitchControl() = default;

QSize SwitchControl::sizeHint() const {
    return QSize(kTrackW, kTrackH);
}

void SwitchControl::onToggled(bool checked) {
    if (skipAnim_) return;  // setCheckedNoAnim path — don't animate
    animateTo(checked ? 1.0 : 0.0);
}

void SwitchControl::setCheckedNoAnim(bool checked) {
    if (anim_ && anim_->state() == QAbstractAnimation::Running) anim_->stop();
    skipAnim_ = true;
    QAbstractButton::setChecked(checked);
    skipAnim_ = false;
    knobPos_ = checked ? 1.0 : 0.0;
    emit knobPosChanged(knobPos_);
    update();
}

void SwitchControl::setKnobPos(qreal p) {
    p = qBound(0.0, p, 1.0);
    if (qFuzzyCompare(knobPos_, p)) return;
    knobPos_ = p;
    emit knobPosChanged(p);
    update();
}

void SwitchControl::animateTo(qreal target) {
    if (anim_ && anim_->state() == QAbstractAnimation::Running) anim_->stop();
    anim_->setStartValue(knobPos_);
    anim_->setEndValue(target);
    anim_->start();
}

// Flat solid colors, no gradients (QSS-safe).
// Track: primary when ON, muted border gray when OFF.
// Knob: white circle.
void SwitchControl::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect r = rect();
    if (r.isEmpty()) return;

    const Theme::PastelPalette& tp = Theme::active();
    const QColor primary = QColor(tp.primary);
    const QColor border = QColor(tp.border);
    const QColor mutedBorder = border.isValid() ? border : QColor("#d4d7e0");

    QColor trackColor;
    if (isChecked())        trackColor = primary;
    else if (hovered_)      trackColor = primary.lighter(175);
    else                    trackColor = mutedBorder;

    QPainterPath track;
    track.addRoundedRect(QRectF(r), kTrackH / 2.0, kTrackH / 2.0);
    p.fillPath(track, trackColor);

    // Knob — 15x15, vertical-centered, horizontally interpolated
    const int yOff = (kTrackH - kKnobD) / 2;
    const int xMin = kPad;
    const int xMax = kTrackW - kKnobD - kPad;
    const qreal kx = xMin + (xMax - xMin) * knobPos_;
    QRectF knobRect(kx, yOff, kKnobD, kKnobD);

    QPainterPath knobPath;
    knobPath.addEllipse(knobRect);
    p.fillPath(knobPath, Qt::white);

    // Subtle inner ring for crispness on HiDPI
    QPen ringPen(QColor(0, 0, 0, 18));
    ringPen.setWidthF(1.0);
    p.setPen(ringPen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(knobPath);
}

void SwitchControl::resizeEvent(QResizeEvent* e) {
    QAbstractButton::resizeEvent(e);
    update();
}

void SwitchControl::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        // Clicking anywhere on the switch flips the state — like the HTML design.
        setChecked(!isChecked());
        e->accept();
        return;
    }
    QAbstractButton::mousePressEvent(e);
}

void SwitchControl::mouseReleaseEvent(QMouseEvent* e) {
    // Swallow release so QAbstractButton doesn't re-toggle.
    e->accept();
}

void SwitchControl::enterEvent(QEnterEvent* e) {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(e);
}

void SwitchControl::leaveEvent(QEvent* e) {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void SwitchControl::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        setChecked(!isChecked());
        e->accept();
        return;
    }
    QAbstractButton::keyPressEvent(e);
}

} // namespace DocuSearch
