#pragma once

// ============================================================
// ModernTooltip.h — app-wide custom tooltip presentation
// ============================================================
// The native QTipLabel is a square, system-colored top-level: no
// shadow, no fade, no elevation — it instantly reads as outdated
// next to the app's Fluent glass surfaces. v1.7.17 replaces the
// PRESENTATION only (every widget keeps its existing toolTip()
// text; nothing had to be re-authored):
//
//   • MainWindow::eventFilter intercepts QEvent::ToolTip app-wide
//     and calls showTip() for widgets that carry a non-empty
//     tooltip. An EMPTY tooltip is passed through untouched — that
//     is exactly how QMenu action tooltips (delivered on the menu,
//     whose own toolTip() is empty) keep working via Qt's native
//     path and the QSS QToolTip chip.
//   • The tip is a frameless, translucent, activate-free top-level
//     painting a rounded glass card: theme-synced fill (setStyle is
//     fed the SAME @tooltipBg/Text/Border@ tokens applyTheme()
//     computes), hairline border, layered soft drop shadow.
//   • It fades + slides in over 130 ms (windowOpacity is compositor
//     work, so the fade stays smooth under load) and flips at
//     screen edges so it is never clipped or under the cursor.
//   • hideTip() fires on leave / press / wheel / moving onto another
//     widget / app deactivation — the same moments Qt hides its own.
//
// Header-only, no Q_OBJECT (wiring is lambda-based, like
// SplashOverlay.h). The singleton tip window is intentionally
// process-lifetime; parentless top-levels are cleaned up by Qt at
// application destruction.
// ============================================================

#include <QWidget>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QPointer>
#include <QPalette>
#include <QFont>
#include <memory>

namespace DocuSearch {

class ModernTooltip : public QWidget {
public:
    // Theme sync — MainWindow::applyTheme passes the exact tokens the
    // QSS QToolTip rule uses, so both tooltip paths stay one visual.
    static void setStyle(const QColor& bg, const QColor& text,
                         const QColor& border) {
        s_bg = bg; s_text = text; s_border = border;
        if (s_instance) {
            // A live tip must follow the theme switch immediately, not
            // only after it is rebuilt: refresh the label palette too.
            QPalette lp = s_instance->m_label->palette();
            lp.setColor(QPalette::WindowText, text);
            lp.setColor(QPalette::Text, text);
            s_instance->m_label->setPalette(lp);
            s_instance->update();
        }
    }

    // Show (or re-position) the tip for `host`. globalPos is the cursor
    // position; the card lands below-right of it, flipping at edges.
    static void showTip(QWidget* host, const QString& text,
                        const QPoint& globalPos) {
        if (!host || text.trimmed().isEmpty()) return;
        if (!s_instance) s_instance = new ModernTooltip();
        s_instance->present(host, text, globalPos);
    }

    // Hide instantly (native tooltip behavior on leave/press).
    static void hideTip() {
        if (s_instance && s_instance->isVisible()) s_instance->hide();
    }

    // Whether a tip is currently on screen (drives the filter's hide
    // conditions without touching Qt's native tooltip state).
    static bool tipVisible() {
        return s_instance && s_instance->isVisible();
    }
    static QWidget* currentHost() {
        return s_instance ? s_instance->m_host.data() : nullptr;
    }
    static QString currentText() {
        return s_instance ? s_instance->m_label->text() : QString();
    }

private:
    static constexpr int kPad    = 12;  // shadow gutter around the card
    static constexpr int kRadius = 10;  // card corner radius
    static constexpr int kMaxTextW = 380;

    ModernTooltip()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        // Invisible to the mouse: the widget under the cursor keeps every
        // event, so hover/leave logic stays with the real target and our
        // own window can never trigger the filter's hide conditions.
        setAttribute(Qt::WA_TransparentForMouseEvents, true);

        m_label = new QLabel(this);
        m_label->setWordWrap(true);
        m_label->setTextInteractionFlags(Qt::NoTextInteraction);
        m_label->setTextFormat(Qt::AutoText);  // plain + rich text both fine
        QFont f = m_label->font();
        f.setPixelSize(12);
        f.setWeight(QFont::Medium);
        m_label->setFont(f);
        QPalette lp = m_label->palette();
        lp.setColor(QPalette::WindowText, s_text);
        lp.setColor(QPalette::Text, s_text);
        m_label->setPalette(lp);
        m_label->setAttribute(Qt::WA_TranslucentBackground, true);

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(kPad + 1, kPad + 1, kPad + 1, kPad + 1);
        lay->addWidget(m_label);
    }

    void present(QWidget* host, const QString& text, const QPoint& globalPos) {
        // Already showing exactly this tip for this widget: Qt re-sends
        // ToolTip events while the cursor jiggles; never re-fade for them.
        if (isVisible() && m_host.data() == host && m_label->text() == text)
            return;
        m_host = host;

        m_label->setText(text);
        // Word-wrapped QLabel sizing: cap the natural width, then ask the
        // label how tall it wraps at that width (QLabel's default
        // sizePolicy carries heightForWidth).
        int w = qMin(m_label->sizeHint().width(), kMaxTextW);
        w = qMax(w, 40);
        const int h = m_label->heightForWidth(w);
        m_label->setFixedSize(w, h > 0 ? h : m_label->sizeHint().height());

        layout()->activate();
        adjustSize();

        // ---- Position: below-right of the cursor, flipped at edges ----
        const QScreen* scr = host->screen()
            ? host->screen() : QGuiApplication::primaryScreen();
        const QRect avail = scr ? scr->availableGeometry()
                                : QRect(0, 0, 1920, 1040);
        QPoint pos = globalPos + QPoint(16, 22);
        if (pos.x() + width() > avail.right() - 4)
            pos.setX(globalPos.x() - width() - 12);      // flip left
        if (pos.y() + height() > avail.bottom() - 4)
            pos.setY(globalPos.y() - height() - 12);     // flip above
        pos.setX(qBound(avail.left() + 4, pos.x(),
                        avail.right() - width() - 4));
        pos.setY(qBound(avail.top() + 4, pos.y(),
                        avail.bottom() - height() - 4));
        m_slideFrom = pos + QPoint(0, 6);
        move(m_slideFrom);

        show();  // WA_ShowWithoutActivating: focus stays where it was

        // Fade + settle: 0 -> 1 opacity while sliding up 6 px.
        if (m_anim) m_anim->stop();
        m_anim = std::make_unique<QVariantAnimation>();
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->setDuration(130);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_anim.get(), &QVariantAnimation::valueChanged, this,
                [this, pos](const QVariant& v) {
                    const double t = v.toDouble();
                    setWindowOpacity(t);
                    // Settle from the slide-start onto the final pos.
                    move(pos.x(),
                         qRound(m_slideFrom.y() + (pos.y() - m_slideFrom.y()) * t));
                });
        m_anim->start();   // owned by m_anim (never DeleteWhenStopped)
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF card = QRectF(rect()).adjusted(kPad, kPad,
                                                    -kPad, -kPad);
        // Layered soft shadow — three translucent passes, deepest last.
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            QPainterPath sp;
            sp.addRoundedRect(card.translated(0, 1.5 + i * 1.5), kRadius, kRadius);
            QColor sc(0, 0, 0, 16 + i * 14);
            p.fillPath(sp, sc);
        }
        // Glass card + hairline border.
        QPainterPath cardPath;
        cardPath.addRoundedRect(card, kRadius, kRadius);
        p.fillPath(cardPath, s_bg);
        p.setPen(QPen(s_border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
    }

    QLabel* m_label = nullptr;
    QPointer<QWidget> m_host;
    QPoint m_slideFrom;
    std::unique_ptr<QVariantAnimation> m_anim;

    // Process-lifetime singleton tip window (created on first showTip).
    inline static ModernTooltip* s_instance = nullptr;

    // Themed presentation — defaults mirror the light @tooltip*@ tokens so
    // a tip shown before the first applyTheme() still looks intentional.
    inline static QColor s_bg    = QColor("#f7f9fc");
    inline static QColor s_text  = QColor("#1c2430");
    inline static QColor s_border = QColor("#d5dde7");
};

} // namespace DocuSearch
