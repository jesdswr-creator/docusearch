#pragma once

// ============================================================
// SwitchControl.h - iOS-style pill toggle, QSS-safe and palette-aware
// ============================================================
//
// Replaces the old "AI: ON/OFF" QPushButton with a slider pill
// matching the Pastel Pop design (36x21, knob slides 3px → 18px).
// Color is read from Theme::active() so it adapts to all 3 palettes.
//
// Emits toggled(bool) exactly like QPushButton.
// ============================================================

#include <QAbstractButton>

class QPropertyAnimation;

namespace DocuSearch {

class SwitchControl : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal knobPos READ knobPos WRITE setKnobPos NOTIFY knobPosChanged)

public:
    explicit SwitchControl(QWidget* parent = nullptr);
    ~SwitchControl() override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

    qreal knobPos() const { return knobPos_; }
    void   setKnobPos(qreal p);

    // Reflect checked state immediately (no animation).
    void setCheckedNoAnim(bool checked);

signals:
    void knobPosChanged(qreal p);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private slots:
    void onToggled(bool checked);

private:
    void animateTo(qreal target);

    qreal knobPos_ = 0.0;   // 0 = OFF (left), 1 = ON (right)
    bool  hovered_ = false;
    bool  skipAnim_ = false;  // set true while setCheckedNoAnim is running
    QPropertyAnimation* anim_ = nullptr;
};

} // namespace DocuSearch
