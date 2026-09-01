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
        m_animTimer.setInterval(16);
        connect(&m_animTimer, &QTimer::timeout, this, [this]() {
            update();   // repaint from the current elapsed time
        });
        m_clock.start();
        m_animTimer.start();
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
