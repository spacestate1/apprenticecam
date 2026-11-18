#include "retire.h"
#include "managers.h"
#include "icons.h"
#include "uilayout.h"
#include "core_bridge.h"
#include "core_ui/core_ui.h"
#include "cp1252.h"
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QHeaderView>
#include <algorithm>
#include <QTimer>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QAbstractItemDelegate>
#include <QRadioButton>
#include <QGridLayout>
#include <QScrollArea>
#include <QAction>
#include <QMenu>
#include <QApplication>
extern "C" {
#include "scene.h"
}

static QIcon tvIcon(int n) { return Icons::icon(QString("#file:OPMANRES_TREE_%1.png").arg(n)); }
enum { ROLE_OP = Qt::UserRole + 1 };   // op index, -1 toolpath group, -2 other rows

/* ------------------------------------------------------------------------------------------------ Toolpaths */
ToolpathsManager::ToolpathsManager(const UiLayout &ui, CommandSink sink, QWidget *parent) : QWidget(parent), sink_(sink) {
    auto *v = new QVBoxLayout(this); v->setContentsMargins(2, 2, 2, 2); v->setSpacing(1);
    for (int tb = 800; tb <= 804; tb += 4) {           /* the two Toolpaths manager toolbars */
        auto *bar = new QToolBar(this); bar->setIconSize(QSize(16, 16)); bar->setMovable(false);
        int img = 0;                                   /* PNG <tb> strip: one 16 px image per button, in order */
        for (const UiTbBtn &b : ui.tbtns) {
            if (b.tb != tb) continue;
            QString strip = QString("#strip:OPMANRES_PNG_%1.png:%2").arg(tb).arg(img++);
            QAction *a = bar->addAction(Icons::icon(Icons::hasImage(strip) ? strip : b.tip), b.tip);   /* no pack: the tooltip names the fallback icon (ours) */
            a->setToolTip(b.tip);
            QString tip = b.tip;
            connect(a, &QAction::triggered, this, [this, tip] { sink_.run(tip); });
            acts_.append(qMakePair(a, tip));
        }
        v->addWidget(bar); bars_ << bar;
    }
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true); tree_->setRootIsDecorated(true); tree_->setIndentation(16);
    tree_->setIconSize(QSize(16, 16)); tree_->setUniformRowHeights(true);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);   /* the rows show core_ui's op multi-selection (refresh) */
    v->addWidget(tree_, 1);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int) {
        if (filling_) return;
        int op = it->data(0, ROLE_OP).toInt();
        if (op == -1) { sel = -1; msel_owner = -9; }    /* the toolpath group row: every operation (sel < 0) */
        else if (op >= 0) {                             /* COpMgrTree::Selchanged: Ctrl toggles, Shift a range (core_ui op_click) */
            Qt::KeyboardModifiers km = QApplication::keyboardModifiers();
            op_click(op, (km & Qt::ControlModifier) != 0, (km & Qt::ShiftModifier) != 0);
        }
        else if (op == -3) { int o = it->parent() ? it->parent()->data(0, ROLE_OP).toInt() : -1;   /* the Parameters row: sel, then the op dialog (mcamui) */
            if (o >= 0) { sel = o; msel_owner = -9; sim = -1; refresh(); emit selectionChanged(); emit paramsRow(o); } return; }
        else if (op == -4) { int o = it->parent() ? it->parent()->data(0, ROLE_OP).toInt() : -1; if (o >= 0) emit toolRow(o); return; }   /* the tool row (mcamui: show_tm, tm_part_sel) */
        else if (op <= -10 && op >= -12) { emit propsRow(-10 - op); return; }   /* COpMgrTree::Selchanged -0x10..-0x12 -> EditGroup(page 0..2) (core_ui st_open) */
        else return;
        sim = -1;
        refresh(); emit selectionChanged();
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) {   /* a double click on an op row: its parameters (mcamui) */
        if (filling_) return;
        int op = it->data(0, ROLE_OP).toInt(); if (op >= 0) emit paramsRow(op);
    });
    /* the right-click (mcamui draw_manager): an op row selects the operation first (a selected one keeps the set), the
       machine / toolpath group rows set the group the Groups items act on (cm_group 2 / 1), Ctrl held shows Solid Drill */
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pt) {
        QTreeWidgetItem *it = tree_->itemAt(pt); int op = it ? it->data(0, ROLE_OP).toInt() : -9;
        cm_group = 0;
        if (it && op >= 0) { if (op_selected(op)) msel_owner = op; else msel_owner = -9; sel = op; sim = -1; refresh(); emit selectionChanged(); }
        else if (it && op == -1) cm_group = 1;
        else if (it && !it->parent()) cm_group = 2;
        cm_ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
        popupMenu(tree_->viewport()->mapToGlobal(pt));
    });
    /* the label edit ends (OnEndlabeledit, core_ui ask_end): Enter / the focus leaving = the text, Esc = no change */
    connect(tree_->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this](QWidget *ed, QAbstractItemDelegate::EndEditHint hint) {
        if (!ask.act) return;
        auto *le = qobject_cast<QLineEdit *>(ed);
        if (hint == QAbstractItemDelegate::RevertModelCache || !le) ask_end(0);
        else { snprintf(ask.v[0], sizeof ask.v[0], "%s", (cm_group == 2 ? Cp1252::fromQ(le->text()) : le->text().toUtf8()).constData()); ask_end(1); }   /* the machine group: ANSI */
        emit renamed();
    });
    updateEnabled();
}
/* 3585 Rename = TVM_EDITLABEL on the clicked group (formats/opmanager_menu.md; mcamui tree_rename_row) */
void ToolpathsManager::beginRename(int group) {
    if (!tree_->topLevelItemCount()) return;
    QTreeWidgetItem *m = tree_->topLevelItem(0), *it = group == 2 ? m : nullptr;
    for (int c = 0; !it && c < m->childCount(); c++) if (m->child(c)->data(0, ROLE_OP).toInt() == -1) it = m->child(c);
    if (!it) return;
    filling_ = true; it->setFlags(it->flags() | Qt::ItemIsEditable); it->setText(0, cm_group == 2 ? Cp1252::toQ(ask.v[0]) : QString::fromUtf8(ask.v[0])); filling_ = false;
    tree_->setFocus(); tree_->editItem(it, 0);
}
void ToolpathsManager::expandAll(bool on) {
    if (!tree_->topLevelItemCount()) return;
    QTreeWidgetItem *m = tree_->topLevelItem(0);
    if (on) m->setExpanded(true);                       /* CM_EXPAND: mach_open = grp_open = 1, every op open; CM_COLLAPSE: the ops */
    for (int c = 0; c < m->childCount(); c++) {
        QTreeWidgetItem *k = m->child(c);
        if (k->data(0, ROLE_OP).toInt() != -1) continue;
        if (on) k->setExpanded(true);
        for (int o = 0; o < k->childCount(); o++) k->child(o)->setExpanded(on);
    }
}
/* OPMANRES menu 780 as core_ui's cm_items (cascade by depth, CM_ALIAS shows another item's children, CM_HIDE / CM_CTRL
   rules, cm_usable): leaves run core_ui cm_run; items whose dialog this front end does not have yet are greyed */
QMenu *ToolpathsManager::popupMenu(const QPoint &global) {
    std::function<bool(int)> qtUsable = [&](int i) -> bool {
        const CMItem &m = cm_items[i];
        if (m.act == CM_SEP) return false;
        if (m.act == CM_MILLPOP && cm_router()) return false;
        if (m.act == CM_ALIAS && !cm_router()) return false;
        if (!cm_has_kids(i)) return cm_leaf_ok(m.act) && (!menuHas || menuHas(m.act));
        int t = cm_kids_of(i);
        for (int k = t + 1; k < CM_N && cm_items[k].depth > cm_items[t].depth; k++)
            if (cm_shown(k) && !cm_has_kids(k) && cm_leaf_ok(cm_items[k].act) && (!menuHas || menuHas(cm_items[k].act))) return true;
        return false;
    };
    std::function<void(QMenu *, int, int)> fill = [&](QMenu *mu, int parent, int depth) {
        for (int i = parent + 1; i < CM_N && cm_items[i].depth >= depth; i++) {
            if (cm_items[i].depth != depth || !cm_shown(i)) continue;
            if (cm_items[i].act == CM_SEP) { mu->addSeparator(); continue; }
            QString t = QString::fromUtf8(cm_items[i].text).replace('&', "&&");
            if (cm_has_kids(i)) { QMenu *sm = mu->addMenu(t); sm->setEnabled(qtUsable(i)); fill(sm, cm_kids_of(i), cm_items[cm_kids_of(i)].depth + 1); }
            else { QAction *a = mu->addAction(t); bool ok = qtUsable(i); a->setEnabled(ok); if (ok) connect(a, &QAction::triggered, this, [this, i] { emit menuRun(i); }); }
        }
    };
    QMenu *m = new QMenu(this); m->setAttribute(Qt::WA_DeleteOnClose);
    fill(m, -1, 0);
    m->popup(global);
    return m;
}

void ToolpathsManager::updateEnabled() { for (auto &a : acts_) a.first->setEnabled(sink_.ok(a.second)); }   /* unported: greyed */
int ToolpathsManager::selected() const { return sel; }

void ToolpathsManager::fillOp(QTreeWidgetItem *opIt, int i) {
    char t[256];
    /* row images: COpMgrTree image list (LoadImageList, opmanres bitmaps) indices from InitTvItem -
       Parameters 6, tool 8, toolpath 0xd (0x29 none); geometry / op / group rows NOT DECODED */
    qc_params_row(i, t, sizeof t);          /* item texts: optree_text.inc (InitTvItem -3 .. -6) */
    auto *a = new QTreeWidgetItem(opIt, QStringList(QString::fromUtf8(t))); a->setIcon(0, tvIcon(6)); a->setData(0, ROLE_OP, -3);   /* -3: the Parameters row */
    qc_tool_row(i, t, sizeof t);
    a = new QTreeWidgetItem(opIt, QStringList(QString::fromUtf8(t))); a->setIcon(0, tvIcon(8)); a->setData(0, ROLE_OP, -4);   /* -4: the tool row */
    qc_geom_row(i, t, sizeof t);
    a = new QTreeWidgetItem(opIt, QStringList(QString::fromUtf8(t))); a->setIcon(0, tvIcon(10)); a->setData(0, ROLE_OP, -2);   /* -5 geometry: 10 */
    qc_path_row(i, t, sizeof t);
    a = new QTreeWidgetItem(opIt, QStringList(QString::fromUtf8(t))); a->setIcon(0, tvIcon(qc_op_npts(i) ? 13 : 41)); a->setData(0, ROLE_OP, -2);
}

void ToolpathsManager::refresh() {
    filling_ = true;
    QSet<int> open; bool machOpen = true, propsOpen = false, grpOpen = true;   /* mcamui: mach_open 1, props_open 0, grp_open 1 */
    if (tree_->topLevelItemCount()) {
        QTreeWidgetItem *m = tree_->topLevelItem(0); machOpen = m->isExpanded();
        for (int c = 0; c < m->childCount(); c++) {
            QTreeWidgetItem *k = m->child(c);
            if (k->data(0, ROLE_OP).toInt() == -1) { grpOpen = k->isExpanded(); for (int o = 0; o < k->childCount(); o++) if (k->child(o)->isExpanded()) open.insert(k->child(o)->data(0, ROLE_OP).toInt()); }
            else if (c == 0) propsOpen = k->isExpanded();
        }
    }
    tree_->clear();
    if (!qc_loaded()) { filling_ = false; return; }
    auto *mach = new QTreeWidgetItem(tree_, QStringList(Cp1252::toQ(mach_grp_name))   /* the group's ANSI name (cp1252.h) */); mach->setIcon(0, tvIcon(49)); mach->setData(0, ROLE_OP, -2);   /* InitTvItem -1: machine group 0x31 */
    auto *props = new QTreeWidgetItem(mach, QStringList("Properties - Mill Default")); props->setIcon(0, tvIcon(51)); props->setData(0, ROLE_OP, -2);   /* -0xf Properties: 0x33 */
    /* Properties children: opmanres 3829 / 3826 / 3827, images 0x34-0x36 (InitTvItem -0x10..-0x12). A mill group has no 4th
       child: -0x13 "Safety zone" (3828) is built only for a wire group with an Agie machine and gated by RunningSafetyZone()
       = false (decomp/opman_tree_all.c l.29239-29262, decomp/mastercam_safetyzone_editgroup.c) */
    static const char *pk[3] = { "Files", "Tool settings", "Stock setup" };
    for (int k = 0; k < 3; k++) { auto *c = new QTreeWidgetItem(props, QStringList(pk[k])); c->setIcon(0, tvIcon(52 + k)); c->setData(0, ROLE_OP, -10 - k); }   /* -10..-13: page k */
    auto *grp = new QTreeWidgetItem(mach, QStringList(QString::fromUtf8(tp_grp_name))); grp->setIcon(0, tvIcon(16)); grp->setData(0, ROLE_OP, -1);   /* -1 toolpath group: 0x10 */
    if (sel < 0) grp->setSelected(true);
    for (int i = 0; i < qc_nops(); i++) {
        char t[256]; qc_op_row(i, t, sizeof t);        /* optree_text.inc (InitTvOpText) */
        bool s = op_selected(i) != 0;                   /* the multi-selection (core_ui op_selected) */
        auto *it = new QTreeWidgetItem(grp, QStringList(QString::fromUtf8(t)));
        it->setIcon(0, tvIcon(qc_op_stale(i) ? (s ? 24 : 19) : s ? 5 : 4));   /* -2 op: 4, selected 5; dirty (op+0x94) 0x13 / selected 0x18 */
        it->setData(0, ROLE_OP, i);
        fillOp(it, i);
        if (s) it->setSelected(true);
        it->setExpanded(open.contains(i));
    }
    mach->setExpanded(machOpen); props->setExpanded(propsOpen); grp->setExpanded(grpOpen);
    filling_ = false;
    updateEnabled();
}

bool ToolpathsManager::clickPropsRow(int page) {   /* tests (ours): the tree row's itemClicked, as a mouse click delivers it */
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        if ((*it)->data(0, ROLE_OP).toInt() == -10 - page && (*it)->parent() && (*it)->parent()->data(0, ROLE_OP).toInt() == -2) { emit tree_->itemClicked(*it, 0); return true; }
    return false;
}

void ToolpathsManager::setOpExpanded(int op, bool on) {
    if (!tree_->topLevelItemCount()) return;
    QTreeWidgetItem *m = tree_->topLevelItem(0);
    for (int c = 0; c < m->childCount(); c++) {
        QTreeWidgetItem *k = m->child(c);
        if (k->data(0, ROLE_OP).toInt() == -1 && op >= 0 && op < k->childCount()) k->child(op)->setExpanded(on);
    }
}

/* ------------------------------------------------------------------------------------------------ Solids */
/* the history tree (mastercam.dll HistDlg, ../decomp/mastercam_histdlg.c; the model and its sources: core_ui
   cui_extrude.c "Solids manager model", moved from viewer/solidsmgr_ui.inc). Rows: the part's Parasolid solids (their
   description, else opmanres 27600 "Solid", image 0 of bitmap 749) with their stored history ops and the "Stop Op" row
   (mc_add_solid_to_dialog, core_ui sm_psolid_* / sm_pop_*), then each body made by Extrude
   ("Solid", expandable, open by default) with its features (text = the op name, else opmanres 27601 + type; image
   type * 4 + 8, selected type * 4 + 10). A click selects the row (sm_sel_kind / sm_sel_idx); a right click opens the
   FUN_180230a10 menu (Solids / Model Prep submenus with the ribbon's solid commands, then the row kind's items; unported
   items greyed, as mcamui). No toolbar: mcamui's Solids tab has none (its toolbar is NOT DECODED). */
enum { ROLE_SMK = Qt::UserRole + 2, ROLE_SMI };          /* row kind (1 body, 2 feature, 3 part solid, 4 its op = solid * 1024 + op, 5 its Stop Op), index */
SolidsManager::SolidsManager(QWidget *p) : QWidget(p) {
    auto *v = new QVBoxLayout(this); v->setContentsMargins(2, 2, 2, 2);
    tree_ = new QTreeWidget(this); tree_->setHeaderHidden(true); tree_->setIconSize(QSize(16, 16)); tree_->setIndentation(16);
    tree_->setUniformRowHeights(true); tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(tree_);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int) {
        if (filling_) return;
        sm_sel_kind = it->data(0, ROLE_SMK).toInt(); sm_sel_idx = it->data(0, ROLE_SMI).toInt();
        refresh();
    });
    auto openFlag = [this](QTreeWidgetItem *it, bool on) {
        int kind = it->data(0, ROLE_SMK).toInt(), b = it->data(0, ROLE_SMI).toInt();
        if (filling_ || (kind != 1 && kind != 3) || b < 0 || b >= 512) return;
        if (kind == 1) sm_body_open[b] = on; else sm_psolid_open[b] = on;
    };
    connect(tree_, &QTreeWidget::itemExpanded, this, [openFlag](QTreeWidgetItem *it) { openFlag(it, true); });
    connect(tree_, &QTreeWidget::itemCollapsed, this, [openFlag](QTreeWidgetItem *it) { openFlag(it, false); });
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pt) {
        QTreeWidgetItem *it = tree_->itemAt(pt);
        int k = it ? it->data(0, ROLE_SMK).toInt() : 0;   /* row kinds: 1 body made here, 2 its feature, 3 part solid, 4 its op, 5 its Stop Op */
        QMenu *m = popupMenu(k == 5 ? 3 : k, k == 2 ? it->data(0, ROLE_SMI).toInt() : -1, tree_->viewport()->mapToGlobal(pt));
        (void)m;
    });
}
QMenu *SolidsManager::popupMenu(int rowKind, int rowFeat, const QPoint &global) {
    QMenu *m = new QMenu(this); m->setAttribute(Qt::WA_DeleteOnClose);
    auto run = [this, rowFeat](int act) {
        auto fn = [act, rowFeat] { sm_menu_act(act, rowFeat); };
        if (withGL) withGL(fn); else fn();
        refresh(); emit acted();
    };
    auto fillSub = [run](QMenu *sm, const SmItem *items, int n) {
        for (int i = 0; i < n; i++) {
            QAction *a = sm->addAction(QString::fromUtf8(items[i].t)); int act = items[i].act;
            a->setEnabled(act != SM_NONE);
            if (act != SM_NONE) QObject::connect(a, &QAction::triggered, sm, [run, act] { run(act); });
            if (items[i].sep) sm->addSeparator();
        }
    };
    SmItem it[24]; int n = sm_menu(rowKind, rowFeat, it, 24);
    for (int i = 0; i < n; i++) {
        if (it[i].act < 0) { QMenu *sm = m->addMenu(QString::fromUtf8(it[i].t));
            if (it[i].act == SM_SUB_SOLIDS) fillSub(sm, SM_SOLIDS, SM_NSOLIDS); else fillSub(sm, SM_PREP, SM_NPREP); }
        else { QAction *a = m->addAction(QString::fromUtf8(it[i].t)); int act = it[i].act; a->setEnabled(act != SM_NONE);
            if (act != SM_NONE) connect(a, &QAction::triggered, this, [run, act] { run(act); }); }
        if (it[i].sep) m->addSeparator();
    }
    m->popup(global);                                  /* non-blocking (the test runs grab it with the window) */
    return m;
}
void SolidsManager::refresh() {
    filling_ = true;
    QSignalBlocker block(tree_);                        /* the rebuild's own expand / collapse are not the user's */
    sm_init();
    tree_->clear();
    QIcon solid = Icons::icon("#strip:opmanres_BMP_749.png:0");
    for (int i = 0; loaded && i < F.nsolids; i++) {          /* the part's solids and their history (core_ui sm_psolid_* / sm_pop_*) */
        auto *it = new QTreeWidgetItem(tree_, QStringList(QString::fromUtf8(sm_psolid_text(i)))); it->setIcon(0, solid);
        it->setData(0, ROLE_SMK, 3); it->setData(0, ROLE_SMI, i);
        if (sm_sel_kind == 3 && sm_sel_idx == i) it->setSelected(true);
        const Solid *so = &F.solids[i];
        for (int k = 0; so->hist_ok && k <= so->nops; k++) {
            if (k == so->pos) {                               /* HistDlg_addStop: "Stop Op" after the history position */
                auto *c = new QTreeWidgetItem(it, QStringList(QString::fromUtf8(SM_STOP_TEXT))); c->setIcon(0, Icons::icon(QString::fromUtf8(SM_STOP_ICON)));
                c->setData(0, ROLE_SMK, 5); c->setData(0, ROLE_SMI, i);
                if (sm_sel_kind == 5 && sm_sel_idx == i) c->setSelected(true);
            }
            if (k == so->nops) break;
            int key_i = i * 1024 + k; bool sel = sm_sel_kind == 4 && sm_sel_idx == key_i; char key[80]; sm_pop_icon(i, k, sel, key, sizeof key);
            auto *c = new QTreeWidgetItem(it, QStringList(QString::fromUtf8(sm_pop_text(i, k)))); c->setIcon(0, Icons::icon(QString::fromUtf8(key)));
            c->setData(0, ROLE_SMK, 4); c->setData(0, ROLE_SMI, key_i);
            if (sel) c->setSelected(true);
        }
        it->setExpanded(i < 512 && sm_psolid_open[i]);
    }
    for (int b = 0; b < nsbodies; b++) {                      /* bodies made here */
        auto *it = new QTreeWidgetItem(tree_, QStringList("Solid")); it->setIcon(0, solid);
        it->setData(0, ROLE_SMK, 1); it->setData(0, ROLE_SMI, b);
        if (sm_sel_kind == 1 && sm_sel_idx == b) it->setSelected(true);
        for (int i = 0; i < nfeats && i < 4096; i++) if (feat_body[i] == b) {
            bool sel = sm_sel_kind == 2 && sm_sel_idx == i; char key[64]; sm_feat_icon(i, sel, key, sizeof key);
            auto *c = new QTreeWidgetItem(it, QStringList(QString::fromUtf8(sm_feat_text(i)))); c->setIcon(0, Icons::icon(QString::fromUtf8(key)));
            c->setData(0, ROLE_SMK, 2); c->setData(0, ROLE_SMI, i);
            if (sel) c->setSelected(true);
        }
        it->setExpanded(b >= 512 || sm_body_open[b]);
    }
    filling_ = false;
}

/* ------------------------------------------------------------------------------------------------ Planes */
/* Planes manager tab: opmanres dialog 457 "Planes" (list 1392); list columns FUN_18040b1f0 (decomp/mastercam_planes.c):
   opmanres 1948 "Name", 59399 "G", 3058 "WCS", 126 "C", 127 "T", 1949 "Display", 1947 "Offset", 1998 "Coordinate";
   rows and cells: PlanesMgrBar FUN_18040b9d0 / SetPlanesListIcons FUN_180412640 (core_ui cui_planes.c planes_mgr_row). NOT DECODED: the row
   icons, selection and the fields under the list. */
PlanesManager::PlanesManager(QWidget *p) : QWidget(p) {
    auto *v = new QVBoxLayout(this); v->setContentsMargins(2, 2, 2, 2);
    auto *t = tab_ = new QTableWidget(0, 8, this);
    t->verticalHeader()->hide(); t->setEditTriggers(QAbstractItemView::NoEditTriggers); t->setSelectionMode(QAbstractItemView::NoSelection);
    v->addWidget(t);
    refresh();
}
void PlanesManager::refresh() {
    QTableWidget *t = tab_; int n = qc_planes_mgr_rows(); char buf[512]; char *cells[8];
    t->setHorizontalHeaderLabels({ "Name", QString::fromUtf8(qc_planes_mgr_g_header()), "WCS", "C", "T", "Display", "Offset", "Coordinate" });
    t->setRowCount(n);
    for (int i = 0; i < n; i++) {
        qc_planes_mgr_row(i, cells, buf, sizeof buf);
        for (int c = 0; c < 8; c++) t->setItem(i, c, new QTableWidgetItem(QString::fromUtf8(cells[c])));
    }
    for (int c = 0; c < 8; c++) { int w0 = t->columnWidth(c); t->resizeColumnToContents(c); t->setColumnWidth(c, std::max(w0, t->columnWidth(c))); }   /* ours: as resizeEvent */
}
void PlanesManager::resizeEvent(QResizeEvent *e) {      /* default widths NOT DECODED: drawn as Name 1/3, the rest shared (as mcamui) */
    QWidget::resizeEvent(e);
    int lw = tab_->viewport()->width(), c0 = lw / 3;
    tab_->setColumnWidth(0, c0); for (int c = 1; c < 8; c++) tab_->setColumnWidth(c, (lw - c0) / 7);
    /* ours (textfit 2026-11-17): a column never narrower than its header / cells (the list scrolls sideways instead) */
    for (int c = 0; c < 8; c++) { int w0 = tab_->columnWidth(c); tab_->resizeColumnToContents(c); tab_->setColumnWidth(c, std::max(w0, tab_->columnWidth(c))); }
}

/* ------------------------------------------------------------------------------------------------ Levels */
/* Levels manager tab - levels_ui.inc: opmanres dialog 459 "Levels" (decomp/mastercam_levels.c): list 1390, columns
   FUN_1802491d0: opmanres 1979 "Number", 3273 "Visible", 1948 "Name", 3275 "Entities", 3274 "Level Set"; rows = the used
   levels with their entity counts and names (the level record, mcam_load.h). NOT DECODED: level sets, the Visible mark, the main-level
   marker, the Level Manager's own row set; the form
   under the list (Number 1359 / Name 1313 / Level set 1315 / Display 1363-1366) is greyed. */
LevelsManager::LevelsManager(QWidget *p) : QWidget(p) {
    auto *v = new QVBoxLayout(this); v->setContentsMargins(2, 2, 2, 2);
    tab_ = new QTableWidget(0, 5, this);
    tab_->setHorizontalHeaderLabels({ "Number", "Visible", "Name", "Entities", "Level Set" });
    tab_->verticalHeader()->hide(); tab_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(tab_, 1);
    /* NM_CLICK on the Visible cell without Ctrl / Shift (FUN_1804775c0 -> FUN_180479960 column 1): toggle (core_bridge qc_level_vis_click) */
    connect(tab_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        if (c != 1 || (QGuiApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) return;
        QTableWidgetItem *n = tab_->item(r, 0); if (!n) return;
        qc_level_vis_click(n->text().toInt()); refresh(); emit levelsChanged(); });
    auto *form = new QWidget(this); auto *g = new QGridLayout(form); g->setContentsMargins(4, 4, 4, 4);
    static const char *lab[3] = { "Number:", "Name:", "Level set:" };
    for (int k = 0; k < 3; k++) { g->addWidget(new QLabel(lab[k]), k, 0); auto *e = new QLineEdit; e->setReadOnly(true); g->addWidget(e, k, 1); }
    static const char *disp[4] = { "Used", "Named", "Used or named", "Range" };
    g->addWidget(new QLabel("Display:"), 3, 0);
    for (int k = 0; k < 4; k++) g->addWidget(new QRadioButton(disp[k]), 3 + k, 1);
    form->setEnabled(false);
    v->addWidget(form);
}
void LevelsManager::resizeEvent(QResizeEvent *e) {      /* default widths FUN_180247e90, u = list width / 40: Number 7u, Visible
                                                           6u, Name width - 32u, Entities 8u, Level Set 7u */
    QWidget::resizeEvent(e);
    /* the list's own size follows from the layout after this event (textfit 2026-11-17: the widths were taken from the
       list's old viewport, e.g. a hidden tab's, and the headers were cut): the next turn of the event loop */
    QTimer::singleShot(0, this, [this] {
        double lw = tab_->viewport()->width(), u = lw / 40.0;
        int w[5] = { (int)(7 * u), (int)(6 * u), (int)(lw - 32 * u), (int)(8 * u), (int)(7 * u) };
        for (int c = 0; c < 5; c++) tab_->setColumnWidth(c, w[c]);
    });
}
void LevelsManager::refresh() {
    int counts[256]; qc_level_counts(counts);
    tab_->setRowCount(0);
    for (int l = 0; l < 256; l++) if (counts[l]) {
        int r = tab_->rowCount(); tab_->insertRow(r);
        tab_->setItem(r, 0, new QTableWidgetItem(QString::number(l)));
        {   /* the Visible mark (FUN_180249e00(row, 1, isVisible), LevelListCtl d4.txt 864-865); its glyph (the custom draw) NOT
               DECODED: a check state here */
            auto *it = new QTableWidgetItem; it->setFlags(Qt::ItemIsEnabled); it->setCheckState(qc_level_visible(l) ? Qt::Checked : Qt::Unchecked);
            tab_->setItem(r, 1, it); }
        tab_->setItem(r, 2, new QTableWidgetItem(QString::fromLatin1(qc_level_name(l))));   /* LevelData::getName (the part's level record) */
        tab_->setItem(r, 3, new QTableWidgetItem(QString::number(counts[l])));
    }
}

/* ------------------------------------------------------------------------------------------------ Recent Functions */
/* MCCore.UI RecentFunctionsManagerView (recentfunctionsmanagerview.baml): an ItemsControl of flat buttons (Padding 8,
   transparent), each the command's small icon (right margin 7) + its tooltip text; a click runs the command.
   StackPanel vertical when the panel is at least 64 px high. Newest first, up to config 4023. */
RecentManager::RecentManager(CommandSink sink, QWidget *p) : QWidget(p), sink_(sink) {
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
    auto *sa = new QScrollArea(this); sa->setWidgetResizable(true); sa->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget; box_ = new QVBoxLayout(inner); box_->setContentsMargins(0, 0, 0, 0); box_->setSpacing(0); box_->addStretch(1);
    sa->setWidget(inner); outer->addWidget(sa);
}
void RecentManager::refresh(const QStringList &recent) {
    while (box_->count() > 1) { QLayoutItem *it = box_->takeAt(0); retireWidget(it->widget()); delete it; }   /* retire.h: a click on a row refreshes this list */
    for (int i = 0; i < recent.size(); i++) {
        auto *b = new QToolButton; b->setAutoRaise(true); b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIcon(Icons::icon(recent[i])); b->setIconSize(QSize(16, 16)); b->setText(recent[i]);
        b->setStyleSheet("QToolButton { padding: 8px; text-align: left; }");
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QString t = recent[i];
        connect(b, &QToolButton::clicked, this, [this, t] { sink_.run(t); });
        box_->insertWidget(i, b);
    }
}
