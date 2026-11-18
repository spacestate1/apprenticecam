#include <algorithm>
#include "icons.h"
#include <QSvgRenderer>
#include <QPainter>
#include <QCoreApplication>
#include "uilayout.h"
#include "core_bridge.h"
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QDir>
#include <cstring>
#include <cstdint>

namespace {
QByteArray pack;              // "MCUIICN1" u32 count, then {u16 len, name, u32 len, PNG} (mcamui.c ipack_find)
bool pack_tried = false;
QHash<QString, QPair<qint64, int>> pack_index;   // name -> offset, length
struct MapEnt { QString s, l; };
QHash<QString, MapEnt> imap; bool imap_tried = false;
QHash<QString, QImage> img_cache;

void loadPack() {
    if (pack_tried) return;
    pack_tried = true;
    QString p = qEnvironmentVariable("MCAMUI_ICON_PACK");
    if (p.isEmpty()) { char b[1100]; p = QString::fromUtf8(qc_config_file(b, sizeof b, "icons.pack")); }
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return;
    pack = f.readAll();
    if (pack.size() <= 12 || memcmp(pack.constData(), "MCUIICN1", 8)) { pack.clear(); return; }
    const unsigned char *d = (const unsigned char *)pack.constData(); size_t n = pack.size(), o = 12;
    uint32_t cnt; memcpy(&cnt, d + 8, 4);
    for (uint32_t i = 0; i < cnt && o + 2 <= n; i++) {
        uint16_t l; memcpy(&l, d + o, 2); o += 2;
        if (o + l + 4 > n) break;
        QString nm = QString::fromUtf8((const char *)d + o, l); o += l;
        uint32_t dl; memcpy(&dl, d + o, 4); o += 4;
        if (o + dl > n) break;
        if (!pack_index.contains(nm)) pack_index.insert(nm, qMakePair((qint64)o, (int)dl));   // first wins (linear search)
        o += dl;
    }
}

QImage loadImage(const QString &file) {             // mcamui.c icon_tex
    if (file.isEmpty()) return QImage();
    auto c = img_cache.find(file); if (c != img_cache.end()) return c.value();
    QImage im;
    QString dir = qEnvironmentVariable("MCAMUI_ICONS");
    if (!dir.isEmpty()) im.load(QDir(dir).filePath(file));
    else {
        loadPack();
        auto it = pack_index.find(file);
        if (it != pack_index.end()) im.loadFromData((const uchar *)pack.constData() + it->first, it->second, "PNG");
    }
    if (!im.isNull()) im = im.convertToFormat(QImage::Format_ARGB32);
    img_cache.insert(file, im);
    return im;
}

void loadMap() {
    if (imap_tried) return;
    imap_tried = true;
    QFile f(uiDir() + "/icon_map.tsv");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine());
        while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
        QStringList p = line.split('\t'); if (p.size() < 2) continue;
        if (!imap.contains(p[0])) imap.insert(p[0], MapEnt{ p[1], p.size() > 2 ? p[2] : QString() });
    }
}

// the images of NAME: small, large (mcamui.c icon_resolve)
void resolve(const QString &name, QImage &s, QImage &l) {
    if (name.startsWith("#file:")) { s = loadImage(name.mid(6)); return; }
    if (name.startsWith("#strip:") || name.startsWith("#strip14:")) {   // "#strip:FILE:index" - a 16 (14) px cell
        bool c14 = name[6] == '1'; int cp = c14 ? 14 : 16;
        QString f = name.mid(c14 ? 9 : 7); int c = f.lastIndexOf(':'); if (c < 0) return;
        int cell = f.mid(c + 1).toInt(); f = f.left(c);
        QImage strip = loadImage(f); if (strip.isNull()) return;
        if ((cell + 1) * cp <= strip.width()) s = strip.copy(cell * cp, 0, cp, qMin(cp, strip.height()));
        return;
    }
    loadMap();
    auto it = imap.find(name);
    if (it != imap.end()) { s = loadImage(it->s); l = loadImage(it->l); }
}

QPixmap faded(const QPixmap &p) {                   // greyed controls draw their image faded (icon_tint alpha 80)
    QPixmap o(p.size()); o.fill(Qt::transparent);
    QPainter g(&o); g.setOpacity(80 / 255.0); g.drawPixmap(0, 0, p); g.end();
    return o;
}

// an image drawn into a W x H box keeping its aspect (gallery images are 64 x 32)
QPixmap fit(const QImage &im, int w, int h) {
    QPixmap o(w, h); o.fill(Qt::transparent);
    double s = qMin((double)w / im.width(), (double)h / im.height());
    int dw = qRound(im.width() * s), dh = qRound(im.height() * s);
    QPainter g(&o); g.setRenderHint(QPainter::SmoothPixmapTransform);
    g.drawImage(QRect((w - dw) / 2, (h - dh) / 2, dw, dh), im); g.end();
    return o;
}
}

namespace Icons {
bool packLoaded() { loadPack(); return !pack.isEmpty(); }

bool hasImage(const QString &name) { QImage s, l; resolve(name, s, l); return !s.isNull() || !l.isNull(); }
QPixmap image(const QString &name) { QImage s, l; resolve(name, s, l); return QPixmap::fromImage(s.isNull() ? l : s); }

// no image: a generated tile (colour from the name, its initials) - mcamui.c draw_icon
QPixmap tile(const QString &name, int w, int h) {
    QPixmap o(w, h); o.fill(Qt::transparent);
    if (h < 10 || w < 10) return o;
    QString nm = name;
    if (nm.startsWith("#file:")) nm = nm.mid(6); else if (nm.startsWith("#strip14:")) nm = nm.mid(9); else if (nm.startsWith("#strip:")) nm = nm.mid(7);
    unsigned hsh = 2166136261u; for (char ch : name.toUtf8()) hsh = (hsh ^ (unsigned char)ch) * 16777619u;
    float hue = (hsh % 360) / 360.0f;
    QColor fill = QColor::fromHsvF(hue, 0.35f, 0.92f), edge = QColor::fromHsvF(hue, 0.55f, 0.62f), ink = QColor::fromHsvF(hue, 0.70f, 0.38f);
    int sz = qMin(w, h); QRectF q((w - sz) / 2.0, (h - sz) / 2.0, sz, sz);
    QString ini;                                       // initials of the first two words (letters / digits)
    bool filename = nm != name;                        // an internal image name: no letters
    int bar = nm.lastIndexOf('|'); if (bar >= 0 && bar + 1 < nm.size()) nm = nm.mid(bar + 1);   // "gallery|item": the item
    bool camel = !filename && !nm.contains(' ') && nm.size() > 1 && nm[1].isLower();             // FT function name: OnFileNew
    if (camel && nm.startsWith("On") && nm.size() > 2 && nm[2].isUpper()) nm = nm.mid(2);
    for (int i = 0; !filename && i < nm.size() && ini.size() < 2; i++) {
        QChar c = nm[i], pc = i ? nm[i - 1] : QChar();
        if ((i == 0 || pc == ' ' || pc == '-' || pc == '/' || pc == '_' || (camel && c.isUpper() && pc.isLower())) && c.isLetterOrNumber())
            ini += c.toUpper();
    }
    if (filename) { fill = QColor(214, 220, 228); edge = QColor(170, 178, 190); }
    QPainter g(&o); g.setRenderHint(QPainter::Antialiasing);
    g.setPen(QPen(edge, 1)); g.setBrush(fill);
    g.drawRoundedRect(q.adjusted(0.5, 0.5, -0.5, -0.5), sz * 0.18, sz * 0.18);
    if (!ini.isEmpty()) {
        QFont f = g.font(); f.setPixelSize(sz >= 24 ? 15 : 10); g.setFont(f);   // font_ui 15 / font_tiny 10
        g.setPen(ink); g.drawText(q, Qt::AlignCenter, ini);
    }
    g.end();
    return o;
}
QPixmap tile(const QString &name, int sz) { return tile(name, sz, sz); }

// the fallback icons (ours, user decision I-1): FreeCAD's LGPL icons, bundled as <exe dir>/icons_oss (map.tsv: label or gallery
// item -> SVG, made by tools/oss_icons.py; LICENSES.txt). Used only when the pack has no image for the name.
QHash<QString, QString> oss_map; bool oss_tried = false;
QString ossFile(const QString &name) {
    if (!oss_tried) {
        oss_tried = true;
        QFile f(QCoreApplication::applicationDirPath() + "/icons_oss/map.tsv");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            while (!f.atEnd()) {
                QString line = QString::fromUtf8(f.readLine()).trimmed(); if (line.isEmpty() || line.startsWith("# ")) continue;   /* '# ' = a comment; keys may start with '#file:' */
                QStringList p = line.split('\t'); if (p.size() >= 2) oss_map.insert(p[0], p[1]);
            }
    }
    QString key = name;
    if (!oss_map.contains(key) && !key.startsWith('#')) {   /* resource names ("#file:..") match exactly only */
        int b = key.indexOf('|'); bool gal = b >= 0; if (gal) key = key.mid(b + 1); int h = key.indexOf('#'); if (h >= 0) key = key.left(h);
        if (gal && oss_map.contains("|" + key)) key = "|" + key;   /* "|name": a gallery item's own entry (a button of that name differs) */
    }
    auto it = oss_map.constFind(key);
    return it == oss_map.constEnd() ? QString() : QCoreApplication::applicationDirPath() + "/icons_oss/" + it.value();
}
QPixmap ossPixmap(const QString &file, int w, int h) {
    QSvgRenderer r(file); if (!r.isValid()) return QPixmap();
    QImage im(w, h, QImage::Format_ARGB32_Premultiplied); im.fill(Qt::transparent);
    QSizeF vb = r.viewBoxF().size(); if (vb.isEmpty()) vb = r.defaultSize();   /* keep the SVG's aspect ratio, centred in the cell (a */
    double k = vb.isEmpty() ? 1.0 : std::min(w / vb.width(), h / vb.height());   /* wide gallery cell would stretch a square icon) */
    QSizeF d = vb.isEmpty() ? QSizeF(w, h) : vb * k;
    QPainter pa(&im); r.render(&pa, QRectF((w - d.width()) / 2, (h - d.height()) / 2, d.width(), d.height())); pa.end();
    return QPixmap::fromImage(im);
}

QIcon icon(const QString &name, int w, int h) {
    QIcon ic;
    if (name.isEmpty()) return ic;
    QImage s, l; resolve(name, s, l);
    auto add = [&](const QPixmap &p) { ic.addPixmap(p, QIcon::Normal); ic.addPixmap(faded(p), QIcon::Disabled); };
    if (s.isNull() && l.isNull()) {                          /* no pack image: the bundled open-source icon (ours), else the tile */
        QString of = ossFile(name);
        if (!of.isEmpty()) { QPixmap p = ossPixmap(of, w, h); if (!p.isNull()) { add(p); if (w == h && w <= 16) add(ossPixmap(of, 32, 32)); return ic; } }
    }
    if (s.isNull() && l.isNull()) {
        add(tile(name, w, h));
        if (w == h && w <= 16) add(tile(name, 32, 32));
        return ic;
    }
    // k = r.h > 20 && tex[1] ? 1 : tex[0] ? 0 : 1 (draw_icon): large boxes take the large image when there is one
    const QImage &pick = (h > 20 && !l.isNull()) || s.isNull() ? l : s;
    add(fit(pick, w, h));
    if (w == h && w <= 16 && !l.isNull()) add(fit(l, 32, 32));
    if (w == h && w > 20 && !s.isNull()) add(fit(s, 16, 16));
    return ic;
}
}
