#pragma once

// ============================================================
// IconUtils.h - SVG icon loading + palette-aware rendering
// ============================================================
//
// Lucide SVGs use stroke="currentColor" which Qt's QSvgRenderer
// doesn't honor. We load the SVG text, replace "currentColor"
// with the actual desired color, then render to a QPixmap.
//
// All icons live in :/icons/lucide/<name>.svg (see resources/app.qrc).
// ============================================================

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QColor>
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>

namespace DocuSearch {

// Load a Lucide SVG resource and render it as a QIcon with the
// specified color. The SVG's "currentColor" is replaced with the
// given color so the icon matches the surrounding palette.
//
// size: pixel size of the rendered pixmap (square).
// Returns an empty QIcon if the SVG file can't be loaded.
inline QIcon loadLucideIcon(const QString& name, const QColor& color, int size = 20) {
    const QString path = QString(":/icons/lucide/%1.svg").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QIcon();
    QString svg = QString::fromUtf8(f.readAll());
    f.close();
    svg.replace("currentColor", color.name());
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid()) return QIcon();
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    renderer.render(&painter);
    return QIcon(pm);
}

// Convenience: render at a specific device-pixel-ratio for crisp
// rendering on HiDPI displays.
inline QPixmap loadLucidePixmap(const QString& name, const QColor& color, int size = 20, qreal dpr = 1.0) {
    const QString path = QString(":/icons/lucide/%1.svg").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QPixmap();
    QString svg = QString::fromUtf8(f.readAll());
    f.close();
    svg.replace("currentColor", color.name());
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid()) return QPixmap();
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    renderer.render(&painter);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

} // namespace DocuSearch
