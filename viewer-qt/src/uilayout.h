// uilayout - the ribbon / menus layout (viewer/ui/layout.txt, written by tools/setup_from_install.py --layout from
// ribbonuires.dll BCGP_RIBBON_XML + opmanres; tools/ui_pack.py) and the command menu strings (viewer/ui/cmd_labels.tsv,
// tools/gen_cmd_labels.py). Same parsing rules as viewer/mcamui.c load_ui / cmd_label.
#pragma once
#include <QString>
#include <QVector>
#include <QHash>

struct UiCmd {
    QString kind, id, text, tip, desc, icon_s, icon_l, ft, dll, entry;
    QVector<UiCmd> sub;
    QString label() const { return text.isEmpty() ? tip : text; }   // mcamui: c->text[0] ? c->text : c->tip
};
struct UiGItem { QString name, desc, icon; };
struct UiElem { bool is_gallery = false; UiCmd cmd; QString gname; int cols = 0, iw = 0, ih = 0; QVector<UiGItem> items; };
struct UiGroup { QString name; QVector<UiElem> el; };
struct UiTab { QString name, context; QVector<UiGroup> g; };
struct UiFileCmd { QString text, desc, icon; };
struct UiTbBtn { int tb = 0, id = 0; QString tip, icon; };

struct UiLayout {
    QVector<UiTab> tabs;
    QVector<UiFileCmd> filecmds;
    QVector<UiTbBtn> tbtns;
    bool load(const QString &path);           // load_ui: TAB / GROUP / BTN / GAL / SUB / GITEM / FILE / TBAR lines
};

// ui dir: --ui DIR, else <exe dir>/ui, else ./ui (mcamui main)
QString uiDir();
void setUiDir(const QString &d);
// cmd_label: entry TAB menu text; missing = the FT function name
QString cmdLabel(const QString &fn);
