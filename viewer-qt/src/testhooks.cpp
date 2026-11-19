#include "plseldlg.h"
#include "testhooks.h"
#include "stockdlg.h"
#include "nestdlg.h"
#include "mainwindow.h"
#include "viewwidget.h"
#include "core_bridge.h"
#include "geopanel.h"
#include "expanel.h"
#include "managers.h"
#include "chaindlg.h"
#include "opdlg.h"
#include "toolmgr.h"
#include "toolwiz.h"
#include "bpdlg.h"
#include "simwin.h"
#include "dialogs8.h"
#include "backstage.h"
#include "textfit.h"
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPainter>
#include <QPointer>
#include <QImage>
#include <QPixmap>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <QFile>
#include <QElapsedTimer>
#include <QMenu>
#include <QLineEdit>
extern "C" {
#include "scene.h"
}
#include "core_ui/core_ui.h"

void qt_snap_probe(const V3 *p);
extern int g_key_press_esc_at, g_key_press_calls;   /* mainwindow.cpp hook_key_press (KeyPress) */
extern bool g_qt_scripting;                           /* mainwindow.cpp: no Extrude panel frame (preview) during a step */                       /* mainwindow.cpp: the snapat: test probe's marker */

namespace TestHooks {

/* mcamui.c dump_state: one line per step */
void dumpState(MainWindow *w, const char *step) {
    int live = 0; for (int i = 0; loaded && i < F.nents; i++) live += F.ents[i].type != 0 && F.ents[i].type != E_SURF && F.ents[i].type != E_NOTE && F.ents[i].type != E_MESH;   /* the wireframe (surfaces: srfdump, notes: notedump) */
    printf("[%s] ents=%d loaded=%d part=%s doc=%s dirty=%d ops=%d camops=%d tools=%d solids=%d tab=%s view=%d shade=%d sel=%d hover=%d undo=%d redo=%d prompt=\"%s\" msg=\"%s\"\n", step, live, loaded,
           loaded && F.path[0] ? F.path : "-", doc_path[0] ? doc_path : "-", doc_dirty, loaded ? F.nops : 0, ncamops, loaded ? F.ntools : 0, loaded ? F.nsolids : 0,
           w->currentTabName().toUtf8().constData(), view_number(vmat), solid_mode, nselected, hover_ent, nundo, nredo, cam_msg, w->lastMessage().toUtf8().constData());
    if (w->pendingPrompt()) printf("    (unsaved-changes prompt is open)\n");
    if (getenv("DUMP_TREE")) for (int i = 0; i < qc_nops(); i++) { char t[256]; qc_op_row(i, t, sizeof t); printf("      %s\n", t); }
    if (show_params) printf("    (params dialog open: %s%s, depth %.3f)\n", edit_camop >= 0 ? "editing " : "", pending.name, pending.p.final_z);   /* mcamui dump_state */
    /* the bodies (mcamui dump_state) and the message box of this step (mcamui: " MESSAGE: " + show_msg cleared) */
    { const char *mt, *mx; if (w->msgShown(&mt, &mx)) printf("    MESSAGE: %s\n", mx); }
    for (int i = 0; i < nsbodies; i++) printf("    body %d %s: %d slab(s), volume %.3f\n", i, sbodies[i].name, sbodies[i].n, body_volume(&sbodies[i]));
    if (getenv("MCAMUI_CHAINDUMP")) chain_dump();          /* tests: the chains after every step (core_ui cui_chain.c, as mcamui) */
    if (getenv("MCAMUI_EXDUMP")) ex_dump();                /* tests: the Extrude command / features / bodies (core_ui cui_extrude.c) */
    if (getenv("MCAMUI_OPDUMP")) op_dump();                /* tests: the operations / the op dialog (core_ui cui_ops.c, as mcamui) */
    if (getenv("MCAMUI_MISCDUMP")) { bp_dump(); vf_dump(); az_dump(); ecp_dump(); misc_dump(); }   /* tests: phase 8's dialogs (core_ui, as mcamui) */
    w->msgClose();
    fflush(stdout);
}

void runScript(MainWindow *w, const QString &script) {
    g_qt_scripting = true;
    for (const QString &stepFull : script.split(';', Qt::SkipEmptyParts)) {
        int c = stepFull.indexOf(':');
        QString st = c < 0 ? stepFull : stepFull.left(c), arg = c < 0 ? QString() : stepFull.mid(c + 1);
        /* the document steps as mcamui.c run_script: through the unsaved-changes prompt (answer:save|discard) */
        if (st == "open") w->request(MainWindow::ACT_OPEN_PATH, arg);
        else if (st == "new" || st == "newpart") w->request(MainWindow::ACT_NEW);
        else if (st == "close") w->request(MainWindow::ACT_CLOSE);
        else if (st == "exit") w->request(MainWindow::ACT_EXIT);
        else if (st == "wait" && !arg.isEmpty()) {                                                   /* wait:MS - the event loop runs (timers, view animations) */
            QElapsedTimer t; t.start(); while (t.elapsed() < arg.toInt()) QApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        else if (st == "save") w->docSave(false);                                                    /* the .mcamui project */
        else if (st == "saveas" && !arg.isEmpty()) {                                                 /* saveas:PATH - the project (Save As dialog) */
            if (qc_is_part_file(arg.toUtf8().constData())) printf("    (part files are never overwritten)\n"); else w->docSaveTo(arg);
        }
        else if (st == "answer") w->answerPrompt(arg == "save" ? 1 : arg == "discard" ? 2 : 0);
        else if (st == "export" && !arg.isEmpty()) { if (QFile::exists(arg)) printf("    (exists)\n"); else w->savePartAs(arg); }   /* a Mastercam part */
        else if (st == "sel" && !arg.isEmpty()) { sel_set_list(arg.toUtf8().constData()); geo_applied = 0; w->redrawView(); }   /* sel:3,4,5 - select entities (sel:none clears) */
        else if ([&] { int h = 0; QByteArray sb = st.toUtf8(), ab = arg.toUtf8();   /* the wireframe steps (core_ui cui_geo_script, as mcamui):
                                                                                         draw / gset / geoline / geocmd / gpset / geopick / geodone / geoesc / geoapply / geolive ... */
                       w->view()->withGL([&] { h = cui_geo_script(sb.constData(), c < 0 ? nullptr : ab.constData()); });
                       if (h) w->refreshAll(); return h != 0; }()) { }
        else if (st == "chainat" && !arg.isEmpty()) {                                                /* chainat:X,Y - a left click on a world point through the click path (mcamui handle_click) */
            float sx, sy; QWidget *vw = w->view()->widget();
            if (ViewInput *in = viewInput(w->view())) in->setViewport();
            last_clicks = 1;
            if (scene_to_screen(vw->width(), vw->height(), arg.section(',', 0, 0).toDouble(), arg.section(',', 1, 1).toDouble(), 0, &sx, &sy)) w->viewClick(sx, sy);
            for (int i = 0; i < ncchains; i++) printf("    chain %d: %d ents cut %.3f / %.3f %s\n", i, cchains[i].n, cchains[i].cut0, cchains[i].cut1, cchains[i].closed ? "closed" : "open");
        }
        else if (st == "levelvis" && !arg.isEmpty()) {                                                /* ours: levelvis:N - a click on level N's Visible cell in the
                                                                                                         Levels manager (core_bridge qc_level_vis_click) */
            qc_level_vis_click(arg.toInt()); w->refreshAll();
            printf("    level %d visible %d (main %d, main_level_vis %d)\n", arg.toInt(), qc_level_visible(arg.toInt()), geo_level, mc_main_level_vis);
        }
        else if (st == "nestescat") {                                                                  /* ours: nestescat:N - an Esc key-down arrives before the N-th
                                                                                                         KeyPress call of the next run (mainwindow.cpp hook_key_press; 0 = none) */
            g_key_press_esc_at = arg.toInt(); g_key_press_calls = 0;
            printf("    nestescat %d\n", g_key_press_esc_at);
        }
        else if (st == "nestkeycalls") { printf("    nest KeyPress calls %d\n", g_key_press_calls); }
        else if (st == "nestdelwin" && !arg.isEmpty()) {                                              /* ours: nestdelwin:X0,Y0,X1,Y1 - a window dragged between two world
                                                                                                         points in the Nesting Results Delete loop (get_event event 0x20) */
            float ax, ay, bx, by; QWidget *vw = w->view()->widget();
            if (ViewInput *in = viewInput(w->view())) in->setViewport();
            if (scene_to_screen(vw->width(), vw->height(), arg.section(',', 0, 0).toDouble(), arg.section(',', 1, 1).toDouble(), 0, &ax, &ay) &&
                scene_to_screen(vw->width(), vw->height(), arg.section(',', 2, 2).toDouble(), arg.section(',', 3, 3).toDouble(), 0, &bx, &by))
                ns_del_window(ax, ay, bx, by);
        }
        else if ((st == "tbat" || st == "tbmove") && !arg.isEmpty()) {                               /* ours: tbat:X,Y a view click / tbmove:X,Y the cursor (Contour Tabs Position...) */
            double X = arg.section(',', 0, 0).toDouble(), Y = arg.section(',', 1, 1).toDouble();
            if (st == "tbmove") tb_move(X, Y);
            else { float sx, sy; QWidget *vw = w->view()->widget(); if (ViewInput *in = viewInput(w->view())) in->setViewport();
                   if (scene_to_screen(vw->width(), vw->height(), X, Y, 0, &sx, &sy)) w->viewClick(sx, sy); }
            w->refreshAll();
        }
        else if (st == "tbkey") { tb_key(arg == "enter"); w->refreshAll(); }                          /* ours: tbkey:enter|esc */
        else if (st == "tbdump") { printf("    tabpos pick %d hover %d (%.4f,%.4f) msg \"%s\" tabs %d n %d:", tb_pick, tb_hok, tb_hx, tb_hy, cam_msg, pending.p.tabs, pending.p.ntab_xy);
            for (int t = 0; t < pending.p.ntab_xy; t++) printf(" (%.4f,%.4f)", pending.p.tab_xy[2 * t], pending.p.tab_xy[2 * t + 1]); printf("\n"); }
        /* the Plane Selection 1085 (core_ui cui_plsel.c): psel / pselp / pselnw / pselctl / pseltext / pseledit / pselpt / pselent / pselesc /
           pseldump (cui_psel_script); ours (Qt): pseldlg = the widgets' dump, pseldlgset:btn=ID | edit=I:TEXT through the widgets,
           pselclickw:X,Y,Z = a left click at a world point's screen position (the view's click path) */
        else if (st == "pseldlg" || st == "pseldlgset") {
            g_qt_scripting = false; w->refreshAll();
            PlaneSelDialog *d = w->planeSel1085();
            if (st == "pseldlg") { if (d && d->isVisible()) d->dump(); else printf("    (no plane selection dialog)\n"); }
            else if (!d || !d->testSet(arg.section('=', 0, 0), arg.section('=', 1))) printf("    (plane selection control %s not settable)\n", arg.toUtf8().constData());
            g_qt_scripting = true;
        }
        else if (st == "pselclickw" && !arg.isEmpty()) {
            float sx, sy; QWidget *vw = w->view()->widget(); if (ViewInput *in = viewInput(w->view())) in->setViewport();
            if (scene_to_screen(vw->width(), vw->height(), arg.section(',', 0, 0).toDouble(), arg.section(',', 1, 1).toDouble(), arg.section(',', 2, 2).toDouble(), &sx, &sy))
                w->viewClick(sx, sy);   /* as tbat */
        }
        else if ([&] { int h = 0; QByteArray sb = st.toUtf8(), ab = arg.toUtf8();
                       w->view()->withGL([&] { h = cui_psel_script(sb.constData(), c < 0 ? nullptr : ab.constData()); }); if (h) w->refreshAll(); return h != 0; }()) { }
        else if (st == "chainact" && !arg.isEmpty()) w->chainAct(arg.toInt());                       /* chainact:ID - a Chaining dialog control (core_ui chain_act) */
        else if (st == "chainclick" && !arg.isEmpty()) {                                             /* ours: chainclick:ID - the control through its widget */
            w->syncChain(); if (!w->chainDialog() || !w->chainDialog()->testClick(arg.toInt())) printf("    (chaining control %s not clickable)\n", arg.toUtf8().constData());
        }
        else if (st == "chaindlg") { w->syncChain(); if (w->chainDialog() && w->chainDialog()->isVisible()) w->chainDialog()->dump(); if (w->chainOptsDialog()) w->chainOptsDialog()->dump(); }   /* ours: the dialogs' controls */
        else if ([&] { int h = 0; QByteArray sb = st.toUtf8(), ab = arg.toUtf8();   /* chainmode / chainopt / chainopts / chaindyn / chains / chaindump ... (core_ui cui_chain.c, as mcamui) */
                       w->view()->withGL([&] { h = cui_chain_script(sb.constData(), c < 0 ? nullptr : ab.constData()); });
                       if (h) w->refreshAll(); return h != 0; }()) { }
        else if ([&] { int h = 0; QByteArray sb = st.toUtf8(), ab = arg.toUtf8();   /* extrude / exset / smmenu / smact / smrows / exdump (core_ui cui_extrude.c, as mcamui) */
                       w->view()->withGL([&] { h = st == "show" ? ex_show_step(ab.constData()) : cui_extrude_script(sb.constData(), c < 0 ? nullptr : ab.constData()); });
                       if (h) w->refreshAll(); return h != 0; }()) { }
        else if (st == "snapw" && !arg.isEmpty()) {                                                  /* ours: snapw:X,Y[,Z] - the AutoCursor snap of a world point's screen
                                                                                                        position in the main view (core_ui cui_ops.c geo_snap); the removed mcamui
                                                                                                        set its main viewport first (vp_main), here ViewInput::setViewport as chainat */
            if (ViewInput *in = viewInput(w->view())) in->setViewport();
            QByteArray ab = arg.toUtf8(); int h = 0;
            w->view()->withGL([&] { h = cui_op_script("snapw", ab.constData()); }); if (h) w->refreshAll();
        }
        else if ([&] { int h = 0; QByteArray sb = st.toUtf8(), ab = arg.toUtf8();   /* gen / editop / params / regen / delop / menu / post / camops / ... + ours opdump / opset / tpset / lkset / tl6410 (core_ui cui_ops.c, as mcamui) */
                       w->view()->withGL([&] { h = cui_op_script(sb.constData(), c < 0 ? nullptr : ab.constData()); }); if (h) w->refreshAll(); return h != 0; }()) { }
        else if (st == "opdlg" || st == "opdlgset" || st == "toollist" || st == "tldlgset" || st == "oppopup" || st == "tpmpopup" || st == "pldlg" || st == "pldlgset") {   /* ours (Qt): the dialogs through their widgets */
            g_qt_scripting = false; w->syncOps();      /* as a frame of mcamui would draw them (the pages' own writes included) */
            if (st == "opdlg") { if (w->opDialog() && w->opDialog()->isVisible()) w->opDialog()->dump(); else printf("    (no op dialog)\n"); }
            else if (st == "opdlgset") { if (!w->opDialog() || !w->opDialog()->testSet(arg.section('=', 0, 0), arg.section('=', 1))) printf("    (op dialog control %s not settable)\n", arg.toUtf8().constData()); }
            else if (st == "toollist") { if (w->toolListDialog() && w->toolListDialog()->isVisible()) w->toolListDialog()->dump(); else printf("    (no tool list)\n"); }
            else if (st == "tldlgset") { if (!w->toolListDialog() || !w->toolListDialog()->isVisible() || !w->toolListDialog()->testSet(arg.section('=', 0, 0), arg.section('=', 1)))   /* N13-32: sort=C / row=R / ok / cancel */
                printf("    (tool list control %s not settable)\n", arg.toUtf8().constData()); w->syncOps(); }
            /* the Planes page's dialogs (core_ui cui_planes.c): pldlg = dump; pldlgset:row=I | choice=K | ok | cancel through the widgets */
            else if (st == "pldlg") { bool any = false; for (PlanesDialog *d : { w->planeSelectDialog(), w->updatePlaneDialog() }) if (d && d->isVisible()) { d->dump(); any = true; } if (!any) printf("    (no plane dialog)\n"); }
            else if (st == "pldlgset") { PlanesDialog *d = w->updatePlaneDialog() && w->updatePlaneDialog()->isVisible() ? w->updatePlaneDialog() : w->planeSelectDialog();
                if (!d || !d->isVisible() || !d->testSet(arg.section('=', 0, 0), arg.section('=', 1))) printf("    (plane dialog control %s not settable)\n", arg.toUtf8().constData());
                w->syncOps(); }
            else if (st == "oppopup") { if (w->opDialog()) w->opDialog()->toolMenu(w->opDialog()->mapToGlobal(QPoint(120, 200))); }   /* menu 1247 open (screenshots) */
            else if (st == "tpmpopup") { cm_group = arg.toInt(); w->toolpathsManager()->popupMenu(w->mapToGlobal(QPoint(150, 300))); }   /* the tree menu open (screenshots) */
            g_qt_scripting = true;
        }
        /* phase 7 (core_ui cui_toolmgr.c / cui_toolwiz.c, as mcamui): show:tm[:LIB] / tmsel / tm* / tmup / tmdown / tmdel / tmfilter /
           tnew / tw* / coolant / cool* / tedit / holder, renum / rnset / rnok / rncancel / rndump / tlrdump / toolrpt; this front
           end's: show:tmkey:K (the modal key path), show:tmsize:W,H (the window), show:tmmenu:L (menu 1225 open) */
        else if (st == "show" && (arg.startsWith("tmkey:") || arg.startsWith("tmsize:") || arg.startsWith("tmmenu:"))) {
            g_qt_scripting = false; w->syncTools();
            if (ToolManagerDialog *d = w->toolManager()) {
                if (arg.startsWith("tmkey:")) { if (!d->testSet("key", arg.mid(6).replace("Ctrl+c", "Ctrl+C").replace("Ctrl+v", "Ctrl+V"))) printf("    (key %s not a Tool Manager key)\n", arg.mid(6).toUtf8().constData()); }
                else if (arg.startsWith("tmsize:")) d->resizeBody(arg.mid(7).section(',', 0, 0).toInt(), arg.mid(7).section(',', 1, 1).toInt() - d->titleH() - 4);
                else d->popupMenu(arg.mid(7).toInt(), d->mapToGlobal(QPoint(300, 200)));
            }
            g_qt_scripting = true;
        }
        else if ([&] { int h = 0; QByteArray sb = st.toUtf8(), ab = arg.toUtf8();
                       w->view()->withGL([&] { h = st == "show" ? cui_tool_show_step(ab.constData()) : cui_rn_script(sb.constData(), c < 0 ? nullptr : ab.constData()); });
                       if (h) w->refreshAll(); return h != 0; }()) { }
        else if (st == "tmdlg" || st == "tmdlgset" || st == "twdlg" || st == "twdlgset" || st == "tfdlg" || st == "tfdlgset" || st == "rndlg" || st == "rndlgset" || st == "cooldlg" || st == "cooldlgset" || st == "tmcolpopup") {
            /* ours (Qt): the tool dialogs through their widgets (dump / KEY=V), as a frame of mcamui would draw them */
            g_qt_scripting = false; w->syncTools(); QApplication::processEvents();
            QString k = arg.section('=', 0, 0), v = arg.section('=', 1); bool ok = true, open = true;
            if (st == "tmdlg") { if (show_tm && w->toolManager()) w->toolManager()->dump(); else open = false; }
            else if (st == "tmdlgset") { ok = show_tm && w->toolManager() && w->toolManager()->testSet(k, v); }
            else if (st == "twdlg") { if (tedit_index > -2 && w->toolWizard()) w->toolWizard()->dump(); else open = false; }
            else if (st == "twdlgset") { ok = tedit_index > -2 && w->toolWizard() && w->toolWizard()->testSet(k, v); }
            else if (st == "tfdlg") { if (tmf_open && w->toolFilter()) w->toolFilter()->dump(); else open = false; }
            else if (st == "tfdlgset") { ok = tmf_open && w->toolFilter() && w->toolFilter()->testSet(k, v); }
            else if (st == "rndlg") { if (show_rn && w->renumberDialog()) w->renumberDialog()->dump(); else open = false; }
            else if (st == "rndlgset") { ok = show_rn && w->renumberDialog() && w->renumberDialog()->testSet(k, v); }
            else if (st == "cooldlg") { if (cool.open && w->coolantDialog()) w->coolantDialog()->dump(); else open = false; }
            else if (st == "cooldlgset") { ok = cool.open && w->coolantDialog() && w->coolantDialog()->testSet(k, v); }
            else if (st == "tmcolpopup") { if (w->toolManager()) w->toolManager()->columnMenu(arg.toInt(), w->toolManager()->mapToGlobal(QPoint(300, 120))); }
            QApplication::processEvents(); w->syncTools(); w->syncOps();
            if (!open) printf("    (no %s)\n", st.toUtf8().constData());
            if (!ok) printf("    (%s %s not settable)\n", st.toUtf8().constData(), arg.toUtf8().constData());
            g_qt_scripting = true;
        }
        else if (st == "show" && arg == "acset") w->showAcSettings();
        /* phase 8 (core_ui cui_backplot.c / cui_verify.c / cui_analyze.c / cui_ecp.c / cui_misc.c, the same steps as mcamui):
           bp / bpstep / bptool / bpplay / bpopt / bptog / bptab / bph / bpset / bpopen + ours, verify / vfseek / vfplay + ours,
           az / azlevel / azcolor / azok / azcancel + ours, ecp / ecpset / ecpok + ours, ask + ours (rename, gridset, zoomwin, paste) */
        else if ([&] { int h = 0; QByteArray sb = st.toUtf8(), ab = arg.toUtf8(); const char *a = c < 0 ? nullptr : ab.constData(); QWidget *vw = w->view()->widget();
                       w->view()->withGL([&] { h = cui_bp_script(sb.constData(), a) || cui_vf_script(sb.constData(), a) || cui_az_script(sb.constData(), a)
                                                   || cui_ecp_script(sb.constData(), a) || cui_lface_script(sb.constData(), a) || cui_misc_script(sb.constData(), a, vw->width(), vw->height())
                                                   || cui_stock_script(sb.constData(), a) || cui_attr_script(sb.constData(), a); });   /* Stock Setup (core_ui cui_stock.c) */
                       if (h) w->refreshAll(); return h != 0; }()) { }
        else if (st == "fsmode" && !arg.isEmpty()) { F.grp_fs_mode = arg.toInt(); F.have_grp = 1; }   /* test: machine group Feed Calculation 0-3 (mcamui) */
        /* ours: the Simulator window (simwin.cpp): simdump, simset:ID=V (a control by its UBVS id; V empty = press), simshot:PNG */
        else if (st == "simdump") { if (vf_on && w->verifyWindow()) { w->verifyWindow()->refresh(); w->verifyWindow()->dump(); } else printf("    (no simulator)\n"); }
        else if (st == "simset") { QString id = arg.section('=', 0, 0), v = arg.section('=', 1);
                                   if (!vf_on || !w->verifyWindow() || !w->verifyWindow()->testSet(id, v)) printf("    (simset %s not settable)\n", arg.toUtf8().constData());
                                   QApplication::processEvents(); }
        else if (st == "simshot") { if (vf_on && w->verifyWindow()) { bool ok = w->verifyWindow()->grabWindow().save(arg); printf("%s %s\n", ok ? "wrote" : "cannot write", arg.toUtf8().constData()); } }
        else if (st == "paritydump") w->parityDump();                                                /* ours: every command and its state (parity_check.sh) */
        else if (st == "show" && arg == "gridset") { show_gridset = 1; w->syncMisc(); }              /* as mcamui */
        else if (st == "show" && arg == "backstage-new") w->showBackstagePage(-1);
        else if (st == "show" && arg.startsWith("backstage:")) w->showBackstagePage(arg.mid(10).toInt());   /* show:backstage:N (entry index) */
        else if (st == "show" && arg == "top") { UiCmd k; k.text = "Top"; k.tip = "Top"; w->localRun("Top", false); }
        else if (st == "show" && arg.startsWith("acsnap:")) { int k = arg.mid(7).section('=', 0, 0).toInt(); if (k >= 0 && k < ACS_N) ac_snap[k] = arg.section('=', 1).toInt(); }   /* show:acsnap:K=V (tests) */
        else if (st == "show" && arg.startsWith("acsang:")) { double a = fabs(arg.mid(7).toDouble()); if (a < 1e-6) a = 1e-6; if (a > 90) a = 90; ac_ang_step = a; }   /* show:acsang:DEG (tests) */
        else if (st == "show" && arg == "gallery") w->showGalleryPopup();                           /* the Toolpaths tab's first palette open */
        else if (st == "show" && arg.startsWith("filter:")) { }                                     /* mcamui: sets tm_text, which nothing reads (dead) */
        else if (st == "show" && arg.startsWith("sim:")) { sel = ncamops ? camops[ncamops - 1]->op_index : -1; sim = arg.mid(4).toLong(); solid_mode = 2; w->refreshAll(); }   /* as mcamui */
        else if (st == "show" && arg == "otpmenu") { g_qt_scripting = false; w->syncOps(); if (w->opDialog()) w->opDialog()->toolMenu(w->opDialog()->mapToGlobal(QPoint(120, 200))); g_qt_scripting = true; }   /* menu 1247 open (= oppopup) */
        else if (st == "show" && arg == "open") w->openDialogShown();                               /* the Open dialog, not modal (screenshots) */
        /* ours (Qt): the phase 8 windows through their widgets (dump / KEY=V), as a frame of mcamui would draw them */
        else if (st == "bpdlg" || st == "bpdlgset" || st == "bpodlg" || st == "bpodlgset" || st == "azdlg" || st == "azdlgset" || st == "cdlgset" || st == "ldlgset" || st == "ecpdlg" || st == "ecpdlgset"
                 || st == "griddlgset" || st == "vcrclick" || st == "renameedit" || st == "keyswin" || st == "stdlg" || st == "stdlgset") {
            g_qt_scripting = false; w->syncMisc(); QApplication::processEvents();
            QString k = arg.section('=', 0, 0), v = arg.section('=', 1); bool ok = true;
            if (st.endsWith("set") && arg.contains('&')) {   /* KEY=V&KEY=V: several controls in one step (the model step's count) */
                for (const QString &kv : arg.split('&')) {
                    QString kk = kv.section('=', 0, 0), vv = kv.section('=', 1); bool o = true;
                    if (st == "ecpdlgset") o = w->ecpDialog() && w->ecpDialog()->testSet(kk, vv);
                    else if (st == "bpodlgset") o = w->bpOptions() && w->bpOptions()->testSet(kk, vv);
                    else if (st == "azdlgset") o = w->analyzeDialog() && w->analyzeDialog()->testSet(kk, vv);
                    else if (st == "griddlgset") o = w->gridDialog() && w->gridDialog()->testSet(kk, vv);
                    else if (st == "stdlgset") o = w->stockDialog() && w->stockDialog()->testSet(kk, vv);
                    else if (st == "bpdlgset") o = w->backplotDialog() && w->backplotDialog()->testSet(kk, vv);
                    if (!o) printf("    (%s %s not settable)\n", st.toUtf8().constData(), kv.toUtf8().constData());
                    QApplication::processEvents(); w->refreshAll();
                }
                k = QString();
            }
            if (k.isEmpty() && st.endsWith("set")) ;
            else if (st == "bpdlg") { if (w->backplotDialog() && w->backplotDialog()->isVisible()) w->backplotDialog()->dump(); else printf("    (no backplot)\n"); }
            else if (st == "bpdlgset") ok = w->backplotDialog() && w->backplotDialog()->isVisible() && w->backplotDialog()->testSet(k, v);
            else if (st == "bpodlg") { if (w->bpOptions() && w->bpOptions()->isVisible()) w->bpOptions()->dump(); else printf("    (no options)\n"); }
            else if (st == "bpodlgset") ok = w->bpOptions() && w->bpOptions()->isVisible() && w->bpOptions()->testSet(k, v);
            else if (st == "azdlg") { if (w->analyzeDialog() && w->analyzeDialog()->isVisible()) w->analyzeDialog()->dump(); else printf("    (no analyze)\n"); }
            else if (st == "azdlgset") ok = w->analyzeDialog() && w->analyzeDialog()->isVisible() && w->analyzeDialog()->testSet(k, v);
            else if (st == "cdlgset") ok = w->colorPicker() && w->colorPicker()->isVisible() && w->colorPicker()->testSet(k, v);
            else if (st == "ldlgset") ok = w->levelPicker() && w->levelPicker()->isVisible() && w->levelPicker()->testSet(k, v);
            else if (st == "ecpdlg") { if (w->ecpDialog() && w->ecpDialog()->isVisible()) w->ecpDialog()->dump(); else printf("    (no ecp)\n"); }
            else if (st == "ecpdlgset") ok = w->ecpDialog() && w->ecpDialog()->isVisible() && w->ecpDialog()->testSet(k, v);
            else if (st == "griddlgset") ok = w->gridDialog() && w->gridDialog()->isVisible() && w->gridDialog()->testSet(k, v);
            else if (st == "stdlg") { if (w->stockDialog() && w->stockDialog()->isVisible()) w->stockDialog()->dump(); else printf("    (no stock setup)\n"); }
            else if (st == "stdlgset") ok = w->stockDialog() && w->stockDialog()->isVisible() && w->stockDialog()->testSet(k, v);
            else if (st == "vcrclick") { BpVcrBar *b = w->vcrBar(); ok = b && b->isVisible(); for (int i = 0; ok && i < BP_NVCR; i++) if (BP_VCR[i].id == arg.toInt()) {
                QPoint pc = b->btnRect(i).center(); QMouseEvent pr(QEvent::MouseButtonPress, QPointF(pc), QPointF(b->mapToGlobal(pc)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent re(QEvent::MouseButtonRelease, QPointF(pc), QPointF(b->mapToGlobal(pc)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(b, &pr); QApplication::sendEvent(b, &re); } }
            else if (st == "renameedit") {                  /* the tree's label editor: type the text, Enter */
                QLineEdit *le = qobject_cast<QLineEdit *>(QApplication::focusWidget()); ok = le != nullptr;
                if (le) { le->setText(arg); QKeyEvent kd(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier); QApplication::sendEvent(le, &kd); }
            }
            else if (st == "keyswin") printf("    keys window %s\n", show_help ? "open" : "closed");
            QApplication::processEvents(); w->refreshAll();
            if (!ok) printf("    (%s %s not settable)\n", st.toUtf8().constData(), arg.toUtf8().constData());
            g_qt_scripting = true;
        }
        else if (st == "expanel") { w->syncExtrude(); if (w->exPanel() && w->exPanel()->isVisible()) w->exPanel()->dump(); else printf("    (no extrude panel)\n"); }   /* ours: the panel's controls */
        else if (st == "expanelset" && !arg.isEmpty()) {                                          /* ours: expanelset:KEY=V - through the panel's widget */
            w->syncExtrude(); if (!w->exPanel()->testSet(arg.section('=', 0, 0), arg.section('=', 1))) printf("    (extrude control %s not settable)\n", arg.toUtf8().constData());
        }
        else if (st == "smpopup") {                                                                  /* ours: smpopup:KIND,FEAT - the Solids manager menu open (screenshots) */
            w->setManagerTab(1); w->solidsManager()->popupMenu(arg.section(',', 0, 0).toInt(), arg.section(',', 1, 1).isEmpty() ? -1 : arg.section(',', 1, 1).toInt(),
                                                               w->solidsManager()->mapToGlobal(QPoint(60, 80)));
        }
        else if (st == "panel") { if (w->geoPanel()) w->geoPanel()->dump(); }                       /* ours: the function panel's controls */
        else if (st == "panelfocus") {                                                                /* ours: panelfocus:K - control K gets the focus, nothing typed */
            if (!w->geoPanel() || !w->geoPanel()->testFocus(arg.toInt())) printf("    (panel control %s not focusable)\n", arg.toUtf8().constData());
        }
        else if ((st == "panelset" || st == "panellock") && arg.contains('=')) {                     /* ours: panelset:K=V - control K changed through its widget */
            bool ok = w->geoPanel() && w->geoPanel()->testSet(arg.section('=', 0, 0).toInt(), arg.section('=', 1), st == "panellock");
            if (!ok) printf("    (panel control %s not settable)\n", arg.toUtf8().constData());
        }                               /* show:acset - AutoCursor Settings (as mcamui) */
        else if (st == "srfpick") { w->view()->withGL([&] { srf_pick_ent(arg.toInt()); }); }          /* a Surfaces command's pick of entity N (cui_surf.c) */
        else if (st == "srfend") { w->view()->withGL([] { srf_end(); }); }                            /* its Esc */
        else if (st == "cadinfo") {                                                                   /* ours: what an import made (tools/tests/cad_compare.sh): levels, extents, solids */
            printf("    extents (%.4f,%.4f,%.4f)-(%.4f,%.4f,%.4f)\n", F.lo.x, F.lo.y, F.lo.z, F.hi.x, F.hi.y, F.hi.z);
            printf("    nents %d nsolids %d nsurfs %d\n", F.nents, F.nsolids, F.nsurfs);
            for (int i = 0; i < F.nlvls; i++) printf("    level %d '%s' vis %d\n", F.lvls[i].level, F.lvls[i].name, F.lvls[i].vis);
            for (int i = 0; i < F.nsolids; i++) printf("    solid %d brep %zu level %d color %d desc '%s'\n", i, F.solids[i].brep_n, F.solids[i].level, F.solids[i].color, F.solids[i].desc);
            for (int i = 0; i < F.nents; i++) { const Ent *E = &F.ents[i]; if (E->type) printf("    ent %d type %d level %d color %d style %d width %g blanked %d\n", i, E->type, E->level, E->color, E->ln_style, E->width, E->blanked); }
        }
        else if (st == "pickw") {                                                                    /* ours (tests): the general selection's pick at a world point (pick_entity), toggled */
            QStringList a = arg.split(","); QWidget *vw = w->view()->widget(); float sx = 0, sy = 0;
            scene_to_screen(vw->width(), vw->height(), a.value(0).toDouble(), a.value(1).toDouble(), a.value(2).toDouble(), &sx, &sy);
            int e = -1; w->view()->withGL([&] { e = pick_entity(sx, sy); if (e >= 0) sel_toggle(e); });
            printf("    pick %d\n", e); w->redrawView();
        }
        else if (st == "geoclickw") {                                                                   /* tests: a left click at a world point through the view (geo_click: the entity / solid pick) */
            QStringList a = arg.split(","); double r[3] = { a.value(0).toDouble(), a.value(1).toDouble(), a.value(2).toDouble() };
            QWidget *vw = w->view()->widget(); float sx = vw->width() * 0.5f, sy = vw->height() * 0.5f; if (!arg.isEmpty()) scene_to_screen(vw->width(), vw->height(), r[0], r[1], r[2], &sx, &sy);   /* no argument: the middle of the view */
            w->view()->withGL([&] { geo_click(sx, sy, r, 0); });
        }
        else if (st == "srfpt") { QStringList a = arg.split(","); if (a.size() == 3) srf_test_pt(a[0].toInt(), a[1].toDouble(), a[2].toDouble()); }
        else if (st == "crvdev") { QStringList a = arg.split(","); if (a.size() == 2) srf_test_dev(a[0].toInt(), a[1].toInt()); }
        else if (st == "crviso") { QStringList a = arg.split(","); if (a.size() == 4) srf_test_iso(a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toDouble()); }
        else if (st == "pmdcase") { QStringList a = arg.split(","); if (a.size() == 2) srf_test_pmdcase(a[0].toInt(), a[1].toUtf8().constData()); }
        else if (st == "crvscan") { srf_test_scan(); }
        else if (st == "meshdump") {                                                                 /* ours: the polygonal meshes (E_MESH, STL import): entity, kind,
                                                                                                        facets, the record's box, the vertices' sum (tools/tests/cad_compare.sh) */
            for (int i = 0; i < F.nmeshes; i++) { const McMesh *M = &F.meshes[i]; int e = M->ent; double lo[3], hi[3], sum = 0; memcpy(lo, M->rec + 0xf8, 24); memcpy(hi, M->rec + 0x110, 24);
                for (int k = 0; k < M->np; k++) for (int q = 0; q < 9; q++) sum += M->p[k].v[q];
                printf("      mesh %d ent %d kind %d facets %d mixed %d level %d color %d box (%.4f,%.4f,%.4f)-(%.4f,%.4f,%.4f) boxset %d sum %.4f %s\n", i, e, M->kind, M->np, M->mixed,
                       e >= 0 && e < F.nents ? F.ents[e].level : M->level, e >= 0 && e < F.nents ? F.ents[e].color : M->color, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], M->rec[0x128], sum,
                       e >= 0 && e < F.nents && !F.ents[e].type ? "deleted" : ""); }
        }
        else if (st == "srfdump") {                                                                  /* the surfaces (cui_surf.c tests): entity, id, kind, flags, sel word, state */
            for (int i = 0; i < F.nsurfs; i++) { const Surf *S = &F.surfs[i]; int e = S->ent;
                printf("      srf %d ent %d idn %d kind %d flags 0x%x sel 0x%x %s%s%s\n", i, e, S->idn, S->kind, S->n > 0x6f ? S->rec[0x6e] | S->rec[0x6f] << 8 : 0,
                       S->rec[4] | S->rec[5] << 8, e >= 0 && e < F.nents && !F.ents[e].type ? "deleted " : "", S->blanked ? "blanked " : "", S->edited ? "edited" : ""); }
        }
        else if (st == "srfhl") {                                                                    /* ours (tests): the shaded face highlight of each surface (scene.c scene_srf_look), the hovered entity */
            static const char *lk[] = { "none", "autohl", "selected", "autohl-selected", "none", "autohl-stipple", "selected-stipple", "autohl-selected-stipple" };
            printf("      srf hl hover %d mode %d cfg %d stipple %d\n", hover_ent, solid_mode, scene_render_cfg(), cfg_stipple_hl);
            for (int i = 0; i < F.nsurfs; i++) { int l = scene_srf_look(i); if (l) printf("      srf hl %d ent %d look %s\n", i, F.surfs[i].ent, lk[l]); }
        }
        else if (st == "ents") {                                                                     /* the live geometry (mcamui) */
            for (int i = 0; i < F.nents; i++) { const Ent *E = &F.ents[i]; if (!E->type || E->type == E_SURF || E->type == E_NOTE || E->type == E_MESH) continue;   /* surfaces: srfdump, notes: notedump, meshes: meshdump */
                if (E->type == E_POINT) printf("      %d point (%.4f,%.4f,%.4f)\n", i, E->a.x, E->a.y, E->a.z);
                else if (E->type == E_LINE) printf("      %d line (%.4f,%.4f,%.4f)-(%.4f,%.4f,%.4f) style %d\n", i, E->a.x, E->a.y, E->a.z, E->b.x, E->b.y, E->b.z, E->ln_style);
                else if (E->type == E_SPLINE) printf("      %d spline class 0x%x (%.4f,%.4f,%.4f)-(%.4f,%.4f,%.4f) style %d\n", i, E->cls, E->a.x, E->a.y, E->a.z, E->b.x, E->b.y, E->b.z, E->ln_style);
                else if (E->type == E_SCRV) { int np = 0; for (int p = E->first_poly; p < E->first_poly + E->npoly && p < F.npolys; p++) np += F.polys[p].count;   /* a surface curve (CONS, Curve Surface): ends, uv points, drawn points */
                    const CurveRec *cr = E->crv >= 0 && E->crv < F.ncrvs ? &F.crvs[E->crv] : NULL; int nuv = 0; double t2 = 0;
                    if (cr && cr->n >= 0xf0) { memcpy(&t2, cr->rec + 0xa0, 8); nuv = (int)t2 + 1; }
                    printf("      %d surface curve (%.4f,%.4f,%.4f)-(%.4f,%.4f,%.4f) uv %d drawn %d level %d color %d style %d\n", i, E->a.x, E->a.y, E->a.z, E->b.x, E->b.y, E->b.z, nuv, np, E->level, E->color, E->ln_style); }
                else if (E->vw) printf("      %d arc c(%.4f,%.4f,%.4f) r %.4f from %.2f sweep %.2f deg style %d view %d s(%.4f,%.4f,%.4f) e(%.4f,%.4f,%.4f)\n", i, E->c.x, E->c.y, E->c.z, E->r, E->a0 * 180 / M_PI, E->sw * 180 / M_PI, E->ln_style, E->vw, E->a.x, E->a.y, E->a.z, E->b.x, E->b.y, E->b.z);
                else printf("      %d arc c(%.4f,%.4f,%.4f) r %.4f from %.2f sweep %.2f deg style %d\n", i, E->c.x, E->c.y, E->c.z, E->r, E->a0 * 180 / M_PI, E->sw * 180 / M_PI, E->ln_style); }
        }
        else if (st == "spldump") {                                                                  /* ours: each spline's record: class, knots / control points or segments, t1 t2, ends */
            cui_spldump();
        }
        else if (st == "splpts") {                                                                   /* ours: each spline's drawn polyline (its point count) */
            for (int i = 0; i < F.nents; i++) { const Ent *E = &F.ents[i]; if (E->type != E_SPLINE) continue; int np = 0;
                for (int p = E->first_poly; p < E->first_poly + E->npoly && p < F.npolys; p++) np += F.polys[p].count;
                printf("      %d spline polyline %d points\n", i, np); }
        }
        else if (st == "snapat" && !arg.isEmpty()) {                                                 /* snapat:X,Y - the AutoCursor snap of window X,Y (tests) */
            /* core_ui geo_snap without an anchor, grid pass on (mcamui's drawing cursor); TEST PROBE: the snapped point
               also gets the decoded draw_fast_marker square (ov_fast_marker) - in the program it is drawn only during point
               entry, which comes with the wireframe commands (phase 3) */
            QWidget *vw = w->view()->widget(); QPoint lp = vw->mapFrom(w, QPoint(arg.section(',', 0, 0).toInt(), arg.section(',', 1, 1).toInt()));
            vp_x = 0; vp_y = 0; vp_w = vw->width(); vp_h = vw->height();
            double wd[3], raw[3]; cui_cursor_world((float)lp.x(), (float)lp.y(), wd, raw);
            V3 p = { raw[0], raw[1], 0 }; int k = geo_snap(&p, NULL, -1, 1);
            printf("    snap %s -> %s kind %d at %.4f,%.4f,%.4f\n", arg.toUtf8().constData(), k ? "snapped" : "none", k ? geo_snap_kind : GK_NONE, p.x, p.y, p.z);
            qt_snap_probe(k && geo_snap_kind != GK_GRID ? &p : nullptr);
            w->redrawView();
        }
        else if (st == "selops" && !arg.isEmpty()) {                                                 /* selops:1,3 - multi-select tree operations (as Ctrl+clicks) */
            memset(op_msel, 0, sizeof op_msel); sel = -1;
            for (const QString &q : arg.split(',')) { int n = q.toInt() - 1; if (n >= 0 && n < 1024) { op_msel[n] = 1; if (sel < 0) sel = n; } }
            msel_owner = sel; printf("    %d ops selected\n", op_nselected()); w->refreshTree();
        }
        else if (st == "key" && !arg.isEmpty()) {                                                    /* key:Ctrl+Z - through Mastercam's keymap */
            QKeySequence ks = QKeySequence::fromString(arg);
            if (!ks.isEmpty()) { QKeyEvent e(QEvent::KeyPress, ks[0].key(), ks[0].keyboardModifiers()); if (!w->keymap(&e)) printf("    (key %s not in the keymap)\n", arg.toUtf8().constData()); }
            printf("    view z %.3f %.3f %.3f h %.3f | sel %d undo %d redo %d\n", vmat[6], vmat[7], vmat[8], vht, nselected, nundo, nredo);
        }
        else if (st == "keyev" && !arg.isEmpty()) {                                                  /* keyev:Escape - a key press + release to the focused
                                                                                                        widget (the view without one): the main window's whole key path */
            QKeySequence ks = QKeySequence::fromString(arg);
            if (!ks.isEmpty()) {
                QWidget *t = QApplication::focusWidget() ? QApplication::focusWidget() : w->view()->widget();
                QKeyEvent d(QEvent::KeyPress, ks[0].key(), ks[0].keyboardModifiers()), u(QEvent::KeyRelease, ks[0].key(), ks[0].keyboardModifiers());
                QApplication::sendEvent(t, &d); QApplication::sendEvent(t, &u);
            }
            printf("    (keyev %s) sel %d zoom_window %d prompt \"%s\"\n", arg.toUtf8().constData(), nselected, zoom_window, cam_msg);
        }
        else if (st == "presel") { cfg_presel = arg.toInt(); printf("    (presel %d)\n", cfg_presel); }   /* config 4001 Allow pre-selection (tests) */
        else if (st == "screen" && !arg.isEmpty()) {                                                 /* screen:X,Y[,Z] - window coordinates of a world point (for MCAMUI_EVENTS) */
            QStringList v = arg.split(','); float sx, sy; QWidget *vw = w->view()->widget();
            if (scene_to_screen(vw->width(), vw->height(), v.value(0).toDouble(), v.value(1).toDouble(), v.value(2).toDouble(), &sx, &sy)) {
                QPoint g = vw->mapTo(w, QPoint((int)lroundf(sx), (int)lroundf(sy)));
                printf("    screen %s -> %d,%d\n", arg.toUtf8().constData(), g.x(), g.y()); }
        }
        else if (st == "view" && !arg.isEmpty()) { view_std_animated(arg.toInt()); w->view()->redraw(); }   /* view:1..7 = the standard views (7 = Isometric) */
        else if (st == "shade" && !arg.isEmpty()) { int k = arg.toInt(); if (k >= 3) scene_shade(k); else solid_mode = k; w->refreshAll(); }   /* shade:0|1|2 - solid_mode; shade:3|4 - Shaded / Outline Shaded (scene_shade) */
        else if (st == "fit") { view_fit(0); w->view()->redraw(); }
        else if (st == "tab" && !arg.isEmpty()) w->setTabByName(arg);                                 /* ours: tab:View raises a ribbon tab */
        else if (st == "mgr" && !arg.isEmpty()) w->setManagerTab(arg.toInt());                       /* ours: mgr:0..4 = Toolpaths .. Recent Functions */
        else if (st == "selop" && !arg.isEmpty()) w->selectOp(arg.toInt() - 1);                       /* selop:N - select tree operation N */
        else if (st == "tpmprops" && !arg.isEmpty()) {                                                 /* ours: tpmprops:K - a click on the Properties child K (0 Files, 1 Tool settings, 2 Stock setup) */
            if (!w->toolpathsManager() || !w->toolpathsManager()->clickPropsRow(arg.toInt())) printf("    (no properties row %s)\n", arg.toUtf8().constData());
            QApplication::processEvents(); w->refreshAll(); }
        else if (st == "opexpand" && !arg.isEmpty()) w->expandOp(arg.toInt());                        /* expand an op in the Toolpaths manager */
        else if (st == "mcfn" && !arg.isEmpty()) w->mcFunction(arg, false);
        else if (st == "copyimage") w->copyImageTest(arg);                                               /* ours: Home > Copy Image (Esc / two corners) */                           /* mcfn:OnViewTop - an FT function */
        else if (st == "cmd" && !arg.isEmpty()) { UiCmd k; k.text = arg; k.tip = arg; w->invoke(k); }  /* ours: cmd:LABEL - a ribbon command by label */
        else if (st == "ribbon" && !arg.isEmpty()) { if (!w->invokeRibbonPath(arg)) printf("    (no ribbon command %s)\n", arg.toUtf8().constData()); }   /* ours: ribbon:Tab/Group/Label - by its button (FT routing) */
        else if (st == "nest" || st == "nestdump" || st == "nestfile" || st == "nestcat" || st == "nestlevels" || st == "nestppi" || st == "nestdelesc" || st == "nesttrim") {                                                  /* ours: Geometry Nesting (core_ui cui_nest.c) - nest = ns_start (as the ribbon), nestdump = the session */
            QByteArray sb = st.toUtf8(), ab = arg.toUtf8(); w->view()->withGL([&] { cui_ns_script(sb.constData(), ab.constData()); });
            g_qt_scripting = false; w->syncMisc(); w->syncChain(); w->refreshAll();
        }
        else if (st == "nestset" || st == "nestdlg") {                                                 /* ours: nestset:ID=V (a control of the top Nesting window through its widget:
                                                                                                         tab=K, menu=LIST,CMD; V "#k" = a combo item), nestdlg = the open windows' controls */
            g_qt_scripting = false; w->syncMisc(); QApplication::processEvents();
            NestDialog *top = nullptr; for (int k = 0; k < 3; k++) if (w->nestDialog(k) && w->nestDialog(k)->isVisible()) top = w->nestDialog(k);
            if (st == "nestdlg") { bool any = false; for (int k = 0; k < 3; k++) if (w->nestDialog(k) && w->nestDialog(k)->isVisible()) { w->nestDialog(k)->dump(); any = true; } if (!any) printf("    (no nesting window)\n"); }
            else for (const QString &kv : arg.split('&')) {
                if (!top || !top->testSet(kv.section('=', 0, 0), kv.section('=', 1))) printf("    (nestset %s not settable)\n", kv.toUtf8().constData());
                QApplication::processEvents(); w->syncMisc(); w->syncChain();
                top = nullptr; for (int k = 0; k < 3; k++) if (w->nestDialog(k) && w->nestDialog(k)->isVisible()) top = w->nestDialog(k);
            }
            w->refreshAll();
        }
        else if (st == "selbarclick" && (arg == "body" || arg == "face")) {                          /* ours: selbarclick:body|face - the Selection Bar's Select body /
                                                                                                         Select face button through its widget (N13-8) */
            w->refreshAll(); if (!w->selBarTestClick(arg == "face")) printf("    (selection bar button %s not enabled)\n", arg.toUtf8().constData());
            printf("    srfsel selbar enabled %d body %d face %d\n", ms_selbar_enabled(), ms_sld_body, ms_sld_faces);
        }
        else if (st == "dump") { }
        else if (st == "textcheck") { g_qt_scripting = false; w->syncOps(); w->syncMisc(); QApplication::processEvents(); if (arg == "pages") TextFit::checkPages("pages"); }   /* ours: textcheck:pages - every page of the open dialogs checked (MCAMUI_TEXTCHECK, textfit.cpp) */
        else { printf("    (unknown step %s)\n", st.toUtf8().constData()); }
        w->runPendingGen();                                /* N13-9: a generation the step queued runs now (worker thread + progress), as OK's */
        QApplication::processEvents();
        dumpState(w, st.toUtf8().constData());
        TextFit::check(stepFull.left(80).toUtf8().constData());   /* tests: MCAMUI_TEXTCHECK (textfit.cpp) */
    }
    g_qt_scripting = false;
    w->redrawView();                                   /* the first frame after the script (mcamui's main loop): the panel / preview */
}

int lastTick(const QString &events) {
    int last = 0;
    for (const QString &t : events.split(';', Qt::SkipEmptyParts)) { int f = t.section(':', 0, 0).toInt(); if (f + 1 > last) last = f + 1; }
    return last;
}

static QPointer<QWidget> g_grab;                       /* the widget a pressed button sends its moves / release to */
static Qt::MouseButtons g_buttons;
static void sendMouse(MainWindow *w, QEvent::Type type, int x, int y, Qt::MouseButton b) {
    QPoint wp(x, y);
    QWidget *target = (g_grab && type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick) ? g_grab.data() : w->childAt(wp);
    if (!target) target = w;
    QPoint lp = target->mapFrom(w, wp), gp = w->mapToGlobal(wp);
    if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) { g_buttons |= b; g_grab = target; }
    Qt::MouseButtons btns = type == QEvent::MouseButtonRelease ? (g_buttons & ~b) : g_buttons;
    QMouseEvent e(type, QPointF(lp), QPointF(gp), type == QEvent::MouseMove ? Qt::NoButton : b, btns, QApplication::keyboardModifiers());
    QApplication::sendEvent(target, &e);
    if (type == QEvent::MouseButtonRelease) { g_buttons &= ~b; if (!g_buttons) g_grab = nullptr; }
}

/* mcamui.c ev_push: the steps of tick F */
void pushEvents(MainWindow *w, const QString &events, int tick) {
    for (const QString &t : events.split(';', Qt::SkipEmptyParts)) {
        QStringList p = t.split(':');
        if (p.size() < 2) continue;
        int f = p[0].toInt(); QString a = p[1], b = p.size() > 2 ? p.mid(2).join(':') : QString();
        QStringList xy = b.split(','); int x = xy.value(0).toInt(), y = xy.value(1).toInt();
        if (a == "clickw" || a == "movew" || a == "downw" || a == "upw" || a == "dblclickw") {   /* a world X,Y (on Z 0) through the current view (mcamui ev_push) */
            float sx = 0, sy = 0; QWidget *vw = w->view()->widget();
            scene_to_screen(vw->width(), vw->height(), xy.value(0).toDouble(), xy.value(1).toDouble(), xy.value(2).toDouble(), &sx, &sy);   /* optional Z (ours) */
            QPoint g = vw->mapTo(w, QPoint((int)lroundf(sx), (int)lroundf(sy))); x = g.x(); y = g.y();
            a = a == "clickw" ? "click" : a == "movew" ? "move" : a == "downw" ? "down" : a == "dblclickw" ? "dblclick" : "up";
        }
        Qt::MouseButton btn = Qt::LeftButton;
        if (a == "rclick") { btn = Qt::RightButton; a = "click"; }
        if (a == "mdown") { btn = Qt::MiddleButton; a = "down"; }
        if (a == "mup") { btn = Qt::MiddleButton; a = "up"; }
        if (a == "dblclick") {                            /* ours: press, release, double-click, release over frames F .. F+3 */
            if (f == tick) { sendMouse(w, QEvent::MouseMove, x, y, btn); sendMouse(w, QEvent::MouseButtonPress, x, y, btn); }
            if (f + 1 == tick) sendMouse(w, QEvent::MouseButtonRelease, x, y, btn);
            if (f + 2 == tick) sendMouse(w, QEvent::MouseButtonDblClick, x, y, btn);
            if (f + 3 == tick) sendMouse(w, QEvent::MouseButtonRelease, x, y, btn);
            continue;
        }
        if (a == "click") {
            if (f == tick) { sendMouse(w, QEvent::MouseMove, x, y, btn); sendMouse(w, QEvent::MouseButtonPress, x, y, btn); }
            if (f + 1 == tick) sendMouse(w, QEvent::MouseButtonRelease, x, y, btn);
            continue;
        }
        if (f != tick) continue;
        if (a == "hold") { if (ViewInput *in = viewInput(w->view())) in->holdAxis = b.toInt(); }
        else if (a == "move") sendMouse(w, QEvent::MouseMove, x, y, btn);
        else if (a == "down") { sendMouse(w, QEvent::MouseMove, x, y, btn); sendMouse(w, QEvent::MouseButtonPress, x, y, btn); }
        else if (a == "up") sendMouse(w, QEvent::MouseButtonRelease, x, y, btn);
        else if (a == "wheel") {                          /* F:wheel:N,X,Y - N notches at window X,Y */
            int n = xy.value(0).toInt(), wx = xy.value(1).toInt(), wy = xy.value(2).toInt();
            sendMouse(w, QEvent::MouseMove, wx, wy, Qt::NoButton);
            QWidget *target = w->childAt(QPoint(wx, wy)); if (!target) target = w;
            QPoint lp = target->mapFrom(w, QPoint(wx, wy));
            QWheelEvent e(QPointF(lp), QPointF(w->mapToGlobal(QPoint(wx, wy))), QPoint(), QPoint(0, n * 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(target, &e);
        } else if (a == "step" && !b.isEmpty()) {   /* F:step:NAME:ARG - one MCAMUI_SCRIPT step at frame F (tests: a dialog control between clicks) */
            runScript(w, b);
        } else if (a == "mcfn" && !b.isEmpty()) {   /* F:mcfn:OnViewTop - an FT function at frame F (the view keys' functions; offscreen
                                                       key events do not reach the window's shortcuts) */
            w->mcFunction(b, false);
        } else if (a == "key" && !b.isEmpty()) {
            QKeySequence ks = QKeySequence::fromString(b);
            if (ks.isEmpty()) continue;
            QKeyCombination kc = ks[0];
            QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : w->view()->widget();
            QKeyEvent d(QEvent::KeyPress, kc.key(), kc.keyboardModifiers()), u(QEvent::KeyRelease, kc.key(), kc.keyboardModifiers());
            QApplication::sendEvent(target, &d); QApplication::sendEvent(target, &u);
        }
    }
}

/* the window as drawn (QWidget::grab; the view's GL image is part of it) -> PNG / BMP by the extension */
bool shot(MainWindow *w, const QString &path) {
    QPixmap pm = w->grab();
    {   /* open popup menus (the graphics right-click menu) are their own windows: drawn in at their place */
        QPainter p(&pm);
        /* open popup menus, the mini toolbar, dialogs: their own windows, drawn in at their place (menus last: on top) */
        /* the stacking of mcamui's windows: the Tool Manager under the wizard under its Coolant dialog / the filter (phase 7);
           topLevelWidgets() has no z-order */
        auto layer = [](QWidget *tl) { QString n = tl->objectName();
            /* phase 8: Backplot / Verify / Analyze / the Edit Common / grid / keymap windows over the others, Backplot Options and
               Conditional Stops over Backplot, the Colors / Levels pickers on top (mcamui draws them last) */
            return n == "toolManager" ? 1 : n == "toolWizard" ? 2 : (n == "coolant" || n == "toolFilter" || n == "renumber" || n == "toolReport") ? 3
                 : (n == "backplot" || n == "verify" || n == "analyze" || n == "ecp" || n == "gridset" || n == "keys" || n == "about") ? 4
                 : (n == "bpOptions" || n == "bpStops") ? 5 : (n == "colors" || n == "levels") ? 6 : 0; };
        QList<QWidget *> tls = QApplication::topLevelWidgets();
        std::stable_sort(tls.begin(), tls.end(), [&](QWidget *a, QWidget *b) { return layer(a) < layer(b); });
        for (int pass = 0; pass < 2; pass++)
            for (QWidget *tl : tls)
                if (tl != w && tl->isVisible() && (qobject_cast<QMenu *>(tl) != nullptr) == (pass == 1) && (pass == 1 || tl->isWindow()) && tl->objectName() != "qtooltip_label")
                    p.drawPixmap(w->mapFromGlobal(tl->mapToGlobal(QPoint(0, 0))), tl->grab());
    }
    bool ok = pm.save(path);
    printf("%s %s\n", ok ? "wrote" : "cannot write", path.toUtf8().constData());
    return ok;
}
}
