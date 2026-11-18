#include "uilayout.h"
#include <QFile>
#include <QTextStream>
#include <QStringList>

static QString g_ui_dir;
QString uiDir() { return g_ui_dir; }
void setUiDir(const QString &d) { g_ui_dir = d; }

// next TAB-separated field (mcamui.c field(): a missing field is "", line ends stripped)
namespace {
struct Fields {
    QStringList f; int i = 0;
    QString next() { return i < f.size() ? f[i++] : QString(); }
};
void readCmd(UiCmd &c, Fields &p) {
    c.kind = p.next(); c.id = p.next(); c.text = p.next(); c.tip = p.next(); c.desc = p.next();
    c.icon_s = p.next(); c.icon_l = p.next(); c.ft = p.next(); c.dll = p.next(); c.entry = p.next();
}
}

bool UiLayout::load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    UiTab *t = nullptr; UiGroup *g = nullptr; UiElem *last = nullptr;
    tabs.reserve(32);
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
        Fields p; p.f = line.split('\t');
        QString tag = p.next();
        if (tag == "TAB" && tabs.size() < 32) {
            tabs.push_back(UiTab()); t = &tabs.back(); t->name = p.next(); t->context = p.next(); g = nullptr; last = nullptr;
        } else if (tag == "GROUP" && t) {
            t->g.push_back(UiGroup()); g = &t->g.back(); g->name = p.next(); last = nullptr;
        } else if ((tag == "BTN" || tag == "GAL") && g) {
            g->el.push_back(UiElem()); last = &g->el.back();
            if (tag == "BTN") readCmd(last->cmd, p);
            else {
                last->is_gallery = true; last->gname = p.next(); last->cmd.desc = p.next();
                last->cols = p.next().toInt(); last->iw = p.next().toInt(); last->ih = p.next().toInt();
                last->cmd.text = last->gname;
            }
        } else if (tag == "SUB" && last) {
            UiCmd c; readCmd(c, p); last->cmd.sub.push_back(c);
        } else if (tag == "GITEM" && last && last->is_gallery) {
            UiGItem it; it.name = p.next(); it.desc = p.next(); it.icon = p.next(); last->items.push_back(it);
        } else if (tag == "FILE" && filecmds.size() < 64) {
            UiFileCmd fc; fc.text = p.next(); fc.desc = p.next(); fc.icon = p.next(); filecmds.push_back(fc);
        } else if (tag == "TBAR" && tbtns.size() < 64) {
            UiTbBtn b; b.tb = p.next().toInt(); b.id = p.next().toInt(); b.tip = p.next(); b.icon = p.next(); tbtns.push_back(b);
        }
    }
    return !tabs.isEmpty();
}

QString cmdLabel(const QString &fn) {
    static QHash<QString, QString> lab; static bool done = false;
    if (!done) {                                   // entry TAB menu text (tools/gen_cmd_labels.py)
        done = true;
        QFile f(g_ui_dir + "/cmd_labels.tsv");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            while (!f.atEnd()) {
                QString line = QString::fromUtf8(f.readLine());
                while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
                int t = line.indexOf('\t'); if (t < 0) continue;
                if (!lab.contains(line.left(t))) lab.insert(line.left(t), line.mid(t + 1));   // first entry wins (mcamui: linear search)
            }
    }
    auto it = lab.find(fn);
    return it == lab.end() ? fn : it.value();
}
