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
// The animation: an indeterminate progress bar (sliding highlight)
// plus a cycling status caption, so startup visibly "does something"
// instead of freezing on a static image.
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
#include <cmath>

namespace DocuSearch {

class SplashOverlay : public QWidget {
public:
    explicit SplashOverlay(QWidget* parent = nullptr)
        : QWidget(parent,
                  Qt::SplashScreen | Qt::FramelessWindowHint
                                 | Qt::WindowStaysOnTopHint) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFixedSize(540, 340);

        // 60 fps-ish indeterminate animation.
        m_animTimer.setInterval(28);
        connect(&m_animTimer, &QTimer::timeout, this, [this]() {
            m_phase += 0.045;
            if (++m_tick % 32 == 0) m_statusIndex =
                (m_statusIndex + 1) % m_statuses.size();
            update();   // repaint
        });
        m_animTimer.start();
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

        // ---- Card: navy rounded rect with a soft drop shadow ----
        const QRectF card(20, 20, width() - 40, height() - 40);
        p.setPen(Qt::NoPen);

        QPainterPath shadow;
        shadow.addRoundedRect(card.translated(0, 4), 18, 18);
        p.fillPath(shadow, QColor(2, 8, 20, 90));

        QPainterPath cardPath;
        cardPath.addRoundedRect(card, 18, 18);
        QLinearGradient bg(card.topLeft(), card.bottomRight());
        bg.setColorAt(0.0, QColor("#0f172a"));
        bg.setColorAt(1.0, QColor("#1c2c50"));
        p.fillPath(cardPath, bg);

        // ---- Magnifier glyph (drawn, not a font/asset dependency) ----
        const QPointF c(card.left() + 52, card.top() + 58);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#60a5fa"), 5, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(c, 17, 17);
        QLineF handle(c.x() + 12, c.y() + 12,
                      c.x() + 24, c.y() + 24);
        p.drawLine(handle);

        // ---- Title + subtitle ----
        p.setPen(QColor("#f8fafc"));
        QFont title = font();
        title.setPixelSize(34);
        title.setBold(true);
        p.setFont(title);
        p.drawText(QRectF(card.left() + 96, card.top() + 30,
                          card.width() - 120, 46),
                   Qt::AlignLeft | Qt::AlignVCenter, "DocuSearch");

        p.setPen(QColor("#94a3b8"));
        QFont sub = font();
        sub.setPixelSize(14);
        p.setFont(sub);
        p.drawText(QRectF(card.left() + 30, card.top() + 108,
                          card.width() - 60, 24),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   "Offline Intelligent Document Search & OCR");

        // ---- Indeterminate progress bar ----
        const QRectF slot(card.left() + 30, card.top() + 176,
                          card.width() - 60, 7);
        QPainterPath slotPath;
        slotPath.addRoundedRect(slot, 3.5, 3.5);
        p.fillPath(slotPath, QColor(255, 255, 255, 28));

        // Sliding highlight: a smooth chunk moving left-to-right,
        // fading in/out at the edges (material-style determinate-less).
        const double travel = slot.width() - kChunk;
        const double t = (std::sin(m_phase * 2.0) + 1.0) / 2.0;
        double x = slot.left() + t * travel;
        double alpha = 1.0;
        // Soften near the ends so the bounce does not look mechanical.
        const double edge = std::min(t, 1.0 - t) * 6.0;
        alpha = std::min(1.0, std::max(0.25, edge));
        const QRectF chunk(std::min(x, slot.right() - kChunk),
                           slot.top(), kChunk, slot.height());
        QPainterPath chunkPath;
        chunkPath.addRoundedRect(chunk, 3.5, 3.5);
        QColor chunkColor("#3b82f6");
        chunkColor.setAlphaF(alpha);
        p.fillPath(chunkPath, chunkColor);

        // ---- Cycling status caption ----
        p.setPen(QColor("#cbd5e1"));
        QFont cap = font();
        cap.setPixelSize(13);
        p.setFont(cap);
        p.drawText(QRectF(card.left() + 30, slot.bottom() + 12,
                          card.width() - 60, 22),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   m_statuses[m_statusIndex]);
    }

private:
    QTimer  m_animTimer;
    double  m_phase       = 0.0;
    int     m_tick        = 0;
    int     m_statusIndex = 0;
    static constexpr double kChunk = 72.0;
    const QStringList m_statuses = {
        "Loading your library...",
        "Preparing AI search...",
        "Almost ready...",
    };
};

} // namespace DocuSearch
