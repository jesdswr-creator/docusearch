#pragma once

// ============================================================
// SplashOverlay.h - code-drawn splash with animated progress bar
// ============================================================
//
// Replaces the static splash.png artwork. Because everything is
// painted in code there is NO baked-in white stroke/outline (the
// old asset's white border was visible on any background), and the
// card renders crisp at every DPI.
//
// v1.7.7 — GLITCH FIX. The v1.7.4 bar bounced left-right with an
// alpha dip at each end, and the event loop was only pumped at six
// construction milestones, so the visible animation advanced in
// irregular jump-cuts: freeze → jump → freeze. Users read that as a
// glitch. Three changes:
//
//   1. The bar is now a MATERIAL-STYLE SWEEP: two chunks chase each
//      other left→right with sine easing. No reversal, no bounce, no
//      mid-bar alpha dip — the exact motion users expect from an
//      indeterminate progress bar.
//   2. Status captions CROSSFADE instead of hard-swapping.
//   3. Both the sweep and the caption are derived from ELAPSED TIME
//      (QElapsedTimer), so every repaint — even one forced by a pump
//      halfway through a heavy constructor step — lands on the true,
//      continuously-advancing frame. MainWindow pumps the event loop
//      at EVERY startup milestone now, so the animation advances in
//      paced steps instead of jump-cuts.
//
// v1.7.6: the splash is THEMED — the card, text and progress bar
// derive from the same palette tokens as the main window, with the
// progress chunk + magnifier rendered in the exact button color
// (@primary@) of the active theme. Defaults below keep the classic
// navy look when no theme is applied.
//
// v1.7.17 — SMOOTHNESS, ROUND TWO. The sweep was already time-based,
// yet users still read the intro as "glitchy". Three remaining causes,
// all fixed here:
//
//   1. HARD EDGES. The splash popped into existence at opacity 1 and
//      hard-closed when the main window appeared. Those two snaps read
//      as visual glitches even when every frame was smooth. The splash
//      now FADES IN over 160 ms on show and FADES OUT over 200 ms via
//      fadeOutAndClose() (windowOpacity runs on the compositor, so the
//      fade stays smooth even while the CPU is busy building the
//      main window underneath).
//   2. FULL-WINDOW REPAINTS. Every animation tick called update() on
//      the whole 540x340 translucent top-level. Translucent windows
//      repaint through the DWM, so repainting 183k pixels 60x per
//      second was the single heaviest startup load — and dropped
//      frames ARE the glitch. Only the strip that actually changes
//      (progress slot + caption line) is invalidated now; the card,
//      title and magnifier are static and repaint on demand only.
//   3. WASTED TICKS. The 16 ms timer kept repainting after close().
//      Ticks are skipped while the splash is not visible.
//
// Header-only, no Q_OBJECT needed (no signals/slots; the timer is
// connected via lambdas inside the class).
// ============================================================

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QLineF>
#include <QStringList>
#include <QElapsedTimer>
#include <QVariantAnimation>
#include <functional>
#include <memory>
#include <cmath>
#include <algorithm>

namespace DocuSearch {

class SplashOverlay : public QWidget {
public:
    // v1.7.6: themed palette tokens (see main.cpp wiring).
    struct ThemeColors {
        QColor cardTop;
        QColor cardBottom;
        QColor title;
        QColor muted;      // subtitle line
        QColor caption;    // cycling status line
        QColor accent;     // button color: progress chunk + magnifier
        QColor slot;       // progress track
        QColor shadow;     // soft drop shadow under the card
    };

    explicit SplashOverlay(QWidget* parent = nullptr)
        : QWidget(parent,
                  Qt::SplashScreen | Qt::FramelessWindowHint
                                 | Qt::WindowStaysOnTopHint) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFixedSize(540, 340);

        // Repaint from the event loop at ~60 fps. The FRAME CONTENT is
        // always derived from m_clock.elapsed() inside paintEvent, so a
        // coalesced or delayed timer can never rewind or freeze the
        // animation — it only means fewer intermediate frames.
        // v1.7.17: start fully transparent; showEvent() fades us in.
        setWindowOpacity(0.0);

        m_animTimer.setInterval(16);
        connect(&m_animTimer, &QTimer::timeout, this, [this]() {
            if (!isVisible()) return;   // no wasted repaints after close
            // v1.7.17: repaint ONLY the strip that changes (slot + caption).
            // The card, title and magnifier are static; invalidating the
            // whole translucent window 60x/s was the dropped-frame glitch.
            update(m_animRect);
        });
        m_clock.start();
        m_animTimer.start();

        // v1.7.17: dirty region = progress slot + caption line, grown by
        // the chunk fade margin and antialiasing bleed. Computed once;
        // the layout is fixed-size so it can never go stale.
        const QRectF card(20, 20, width() - 40, height() - 40);
        const QRectF slot(card.left() + 30, card.top() + 176,
                          card.width() - 60, 7);
        QRectF anim(slot.adjusted(-8, -8, 8, 8));
        anim |= QRectF(card.left() + 30, slot.bottom() + 12 - 6,
                       card.width() - 60, 22 + 12);
        m_animRect = anim.toAlignedRect().adjusted(-2, -2, 2, 2);
    }

    ~SplashOverlay() override {
        if (m_fade) m_fade->stop();
        m_animTimer.stop();
    }

    // v1.7.17: fade the splash out (200 ms) and close it. The optional
    // callback runs after close() — main.cpp uses it to quit() on the
    // constructor-failure path so the fade is never cut short.
    // Safe to call from anywhere the event loop is running; a hidden
    // splash closes immediately (nothing to animate).
    void fadeOutAndClose(std::function<void()> after = {}) {
        m_fadeOutDone = std::move(after);
        if (!isVisible()) {
            close();
            if (m_fadeOutDone) m_fadeOutDone();
            return;
        }
        if (m_fade) m_fade->stop();
        m_fade = std::make_unique<QVariantAnimation>();
        m_fade->setStartValue(windowOpacity());
        m_fade->setEndValue(0.0);
        m_fade->setDuration(200);
        m_fade->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_fade.get(), &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
        connect(m_fade.get(), &QVariantAnimation::finished, this, [this]() {
            close();
            m_animTimer.stop();
            if (m_fadeOutDone) {
                auto done = std::move(m_fadeOutDone);
                m_fadeOutDone = nullptr;
                done();
            }
        });
        // NOT DeleteWhenStopped: m_fade (unique_ptr) owns the animation;
        // DeleteWhenStopped would double-delete it at stop.
        m_fade->start();
    }

    // Apply the active theme's colors (call before show()).
    void setThemeColors(const ThemeColors& c) {
        m_colors = c;
        update();
    }

protected:
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
        // Center on the screen the cursor is on (multi-monitor safe).
        const QScreen* scr = screen()
            ? screen() : QGuiApplication::primaryScreen();
        if (scr) {
            const QRect avail = scr->availableGeometry();
            move(avail.center() - QRect(0, 0, width(), height()).center());
        }
        // v1.7.17: fade in — the splash grows out of the desktop instead
        // of snapping into existence (the first half of the "glitch").
        if (m_fade) m_fade->stop();
        m_fade = std::make_unique<QVariantAnimation>();
        m_fade->setStartValue(0.0);
        m_fade->setEndValue(1.0);
        m_fade->setDuration(160);
        m_fade->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_fade.get(), &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { setWindowOpacity(v.toDouble()); });
        m_fade->start();   // owned by m_fade (see fadeOutAndClose note)
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // ---- Card: rounded rect with a soft drop shadow (themed) ----
        const QRectF card(20, 20, width() - 40, height() - 40);
        p.setPen(Qt::NoPen);

        QPainterPath shadow;
        shadow.addRoundedRect(card.translated(0, 4), 18, 18);
        p.fillPath(shadow, m_colors.shadow);

        QPainterPath cardPath;
        cardPath.addRoundedRect(card, 18, 18);
        QLinearGradient bg(card.topLeft(), card.bottomRight());
        bg.setColorAt(0.0, m_colors.cardTop);
        bg.setColorAt(1.0, m_colors.cardBottom);
        p.fillPath(cardPath, bg);

        // ---- Magnifier glyph (drawn, not a font/asset dependency) ----
        const QPointF c(card.left() + 52, card.top() + 58);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(m_colors.accent, 5, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(c, 17, 17);
        QLineF handle(c.x() + 12, c.y() + 12,
                      c.x() + 24, c.y() + 24);
        p.drawLine(handle);

        // ---- Title + subtitle (themed) ----
        p.setPen(m_colors.title);
        QFont title = font();
        title.setPixelSize(34);
        title.setBold(true);
        p.setFont(title);
        p.drawText(QRectF(card.left() + 96, card.top() + 30,
                          card.width() - 120, 46),
                   Qt::AlignLeft | Qt::AlignVCenter, "DocuSearch");

        p.setPen(m_colors.muted);
        QFont sub = font();
        sub.setPixelSize(14);
        p.setFont(sub);
        p.drawText(QRectF(card.left() + 30, card.top() + 108,
                          card.width() - 60, 24),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   "Offline Intelligent Document Search & OCR");

        // ---- Indeterminate progress bar: material-style sweep ----
        const QRectF slot(card.left() + 30, card.top() + 176,
                          card.width() - 60, 7);
        QPainterPath slotPath;
        slotPath.addRoundedRect(slot, 3.5, 3.5);
        p.fillPath(slotPath, m_colors.slot);

        // v1.7.7: two chunks chase each other across the slot. Each chunk
        // owns 60% of the cycle, offset by 40%, so while one is easing
        // out on the right the next is easing in from the left. Sine
        // easing means smooth acceleration into and deceleration out of
        // every sweep — nothing ever reverses direction or dips in
        // opacity mid-bar, which is what made the old bounce look
        // glitchy. Everything derives from elapsed time, so sparse
        // repaints during startup still land on the correct frame.
        constexpr double kPi    = 3.14159265358979323846;
        const qint64  elapsedMs = m_clock.elapsed();
        const double  cycleT    = 2400.0;                      // ms per cycle
        const double  u         = std::fmod(
            double(elapsedMs), cycleT) / cycleT;               // 0..1
        constexpr double kChunkW = 96.0;
        const double  travel    = slot.width() - kChunkW;

        auto sweepChunk = [&](double start, double span) {
            // Chunk active for u in [start, start+span].
            if (u < start || u >= start + span || travel <= 0.0) return;
            const double a   = (u - start) / span;             // 0..1
            const double pos = 0.5 - 0.5 * std::cos(kPi * a);  // easeInOutSine
            // Fade in over the first 12% and out over the last 12% of
            // the chunk's own window; fully opaque in between.
            const double fadeIn  = std::clamp(a / 0.12, 0.0, 1.0);
            const double fadeOut = std::clamp((1.0 - a) / 0.12, 0.0, 1.0);
            const double alpha   = std::min(fadeIn, fadeOut);
            if (alpha <= 0.0) return;

            const QRectF chunk(slot.left() + pos * travel, slot.top(),
                               kChunkW, slot.height());
            QPainterPath chunkPath;
            chunkPath.addRoundedRect(chunk, 3.5, 3.5);
            QColor chunkColor(m_colors.accent);   // the button color
            chunkColor.setAlphaF(alpha);
            p.fillPath(chunkPath, chunkColor);
        };
        sweepChunk(0.00, 0.60);   // leading chunk
        sweepChunk(0.40, 0.60);   // chasing chunk (classic material overlap)

        // ---- Cycling status caption with crossfade (themed) ----
        // v1.7.7: captions used to hard-swap, which read as a flash.
        // Each line owns kCaptionMs; during the first kCaptionBlendMs of
        // a line's window the PREVIOUS line is still drawn, fading out
        // while the new one fades in — a true crossfade instead of a pop.
        p.setFont([&] { QFont f = font(); f.setPixelSize(13); return f; }());
        constexpr double kCaptionMs      = 2600.0;
        constexpr double kCaptionBlendMs = 300.0;
        const double  capU    = std::fmod(double(elapsedMs), kCaptionMs);
        const int     n       = static_cast<int>(m_statuses.size());
        const int     idx     = int(double(elapsedMs) / kCaptionMs) % n;
        const int     prevIdx = (idx + n - 1) % n;

        auto drawCaption = [&](int line, double alpha) {
            if (alpha <= 0.0) return;
            QColor c(m_colors.caption);
            c.setAlphaF(alpha);
            p.setPen(c);
            p.drawText(QRectF(card.left() + 30, slot.bottom() + 12,
                              card.width() - 60, 22),
                       Qt::AlignLeft | Qt::AlignVCenter, m_statuses[line]);
        };
        if (capU < kCaptionBlendMs) {
            const double t = capU / kCaptionBlendMs;   // 0..1 blend
            drawCaption(prevIdx, 1.0 - t);
            drawCaption(idx,     t);
        } else {
            drawCaption(idx, 1.0);
        }
    }

private:
    QTimer         m_animTimer;
    QElapsedTimer  m_clock;        // time-based animation source
    // v1.7.17: fade animation + the dirty strip repainted per tick.
    std::unique_ptr<QVariantAnimation> m_fade;
    std::function<void()> m_fadeOutDone;
    QRect           m_animRect;
    // v1.7.6 themed colors — defaults = the classic navy splash.
    ThemeColors m_colors = {
        QColor("#0f172a"),          // cardTop
        QColor("#1c2c50"),          // cardBottom
        QColor("#f8fafc"),          // title
        QColor("#94a3b8"),          // muted subtitle
        QColor("#cbd5e1"),          // caption
        QColor("#3b82f6"),          // accent (chunk + magnifier)
        QColor(255, 255, 255, 28),  // slot
        QColor(2, 8, 20, 90),       // shadow
    };
    const QStringList m_statuses = {
        "Loading your library...",
        "Preparing AI search...",
        "Almost ready...",
    };
};

} // namespace DocuSearch
