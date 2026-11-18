/* contrast.h - text colours kept readable on the desktop theme's backgrounds (ours, 2026-11-08; user request "make sure the
   toolpath dialogs' text contrasts well against the backgrounds"). The app's fixed text colours (mcamui's greyed rgb(160,160,160),
   the "(ours)" marker, the error red) were picked for a white / light grey background; on a theme with mid-tone backgrounds (the
   user's KDE CDE scheme: window rgb(174,178,195), view rgb(147,151,165)) they wash out. readableOn() keeps a colour where it
   already reaches the WCAG 2 contrast ratio against the background and otherwise darkens or lightens it (keeping its hue) just
   enough to reach it. Ratios used: 4.5 for normal text (WCAG AA), 3.0 for greyed text (still visibly dimmer than normal text). */
#pragma once
#include <QColor>
#include <cmath>

static inline double relLum(const QColor &c) {
    auto ch = [](double v) { v /= 255.0; return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.red()) + 0.7152 * ch(c.green()) + 0.0722 * ch(c.blue());
}
static inline double contrastRatio(const QColor &a, const QColor &b) {
    double la = relLum(a), lb = relLum(b);
    return (std::fmax(la, lb) + 0.05) / (std::fmin(la, lb) + 0.05);
}
/* fg moved toward black or white (whichever needs the smaller step) until its ratio against bg is >= minRatio */
static inline QColor readableOn(const QColor &fg, const QColor &bg, double minRatio) {
    if (contrastRatio(fg, bg) >= minRatio) return fg;
    auto mix = [&](const QColor &to, double t) {
        return QColor((int)std::lround(fg.red() + (to.red() - fg.red()) * t), (int)std::lround(fg.green() + (to.green() - fg.green()) * t),
                      (int)std::lround(fg.blue() + (to.blue() - fg.blue()) * t));
    };
    QColor best; double bestT = 2;
    for (const QColor &to : { QColor(0, 0, 0), QColor(255, 255, 255) }) {
        for (int k = 1; k <= 100; k++) {
            double t = k / 100.0;
            if (contrastRatio(mix(to, t), bg) >= minRatio) { if (t < bestT) { bestT = t; best = mix(to, t); } break; }
        }
    }
    if (best.isValid()) return best;
    return contrastRatio(QColor(0, 0, 0), bg) >= contrastRatio(QColor(255, 255, 255), bg) ? QColor(0, 0, 0) : QColor(255, 255, 255);
}
static inline QString cssColor(const QColor &c) { return QString("color: rgb(%1,%2,%3);").arg(c.red()).arg(c.green()).arg(c.blue()); }
