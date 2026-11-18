// apprenticecam - the Qt 6 Widgets UI of apprenticeCAM (formats/qt_port_plan.md).
//   ./apprenticecam [PART.mcam|.mcx-9] [--ui DIR] [--shot OUT.png --size WxH] [--tab NAME] [--mcp]
#include "mainwindow.h"
#include "testhooks.h"
#include "textfit.h"
#include "mcpserver.h"
#include "viewwidget.h"
#include "uilayout.h"
#include "core_bridge.h"
#include "core_ui/core_ui.h"
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QFontDatabase>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <cstdio>
extern "C" {
#include "scene.h"
#include "contrast.h"
}

/* the use-at-your-own-risk agreement (../LICENSE): asked at every start; MCAMUI_I_ACCEPT_THE_RISK=1 skips it (scripts).
   Text: the removed mcamui.c draw_risk_agreement (backups/pre_nuklear_removal_2026-10-22.tgz). With --shot (ours, tests:
   tools/tests/screenshots/scenes.txt risk_agreement) the dialog is shot to that file and refused, so the run ends. */
static bool risk_agreement(const QString &shot) {
    QDialog d; d.setWindowTitle("WARNING - use at your own risk"); d.setModal(true); d.setFixedWidth(640);
    auto *v = new QVBoxLayout(&d);
    auto *head = new QLabel("YOU ARE INSANE IF YOU USE THIS ON A REAL MACHINE RIGHT NOW. USE AT YOUR OWN RISK.");
    head->setWordWrap(true); head->setStyleSheet("color: rgb(200,0,0); font-weight: bold;");
    v->addWidget(head);
    static const char *para[] = {
        "apprenticeCAM is an experimental, unfinished program.",
        "The toolpaths, G-code, posted programs and part files it generates are UNVERIFIED and may be WRONG: wrong depths, "
        "wrong heights, rapids through stock or fixtures, wrong tool numbers or offsets, wrong spindle direction, missing "
        "coolant, or anything else.",
        "Running its output on a CNC machine can crash the machine, break tools, ruin parts and fixtures, start fires, and "
        "seriously injure or kill people. If you do it anyway, you alone are responsible. Always verify every program "
        "yourself (simulation, dry run above the part, single block, feed override at zero) before cutting anything.",
        "Licensed under the MIT License (see LICENSE): provided \"AS IS\", without warranty of any kind." };
    for (const char *p : para) { auto *l = new QLabel(p); l->setWordWrap(true); v->addWidget(l); }
    v->addSpacing(10);
    auto *h = new QHBoxLayout;
    auto *ok = new QPushButton("I understand and accept all risk"), *ex = new QPushButton("Exit");
    ok->setMinimumHeight(34); ex->setMinimumHeight(34);
    h->addWidget(ok); h->addWidget(ex); v->addLayout(h);
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    QObject::connect(ex, &QPushButton::clicked, &d, &QDialog::reject);
    if (!shot.isEmpty()) QTimer::singleShot(0, &d, [&] { d.grab().save(shot); d.reject(); });
    return d.exec() == QDialog::Accepted;
}

int main(int argc, char **argv) {
    /* ours (user request 2026-10-24: dialogs open at the program's right-hand edge, MainWindow::placeRight): a Wayland
       compositor ignores a program's own window positions, XWayland (xcb) honours them. An explicit QT_QPA_PLATFORM
       wins; APPRENTICECAM_WAYLAND=1 keeps the native Wayland platform. */
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && !qEnvironmentVariableIntValue("APPRENTICECAM_WAYLAND") &&
        qEnvironmentVariableIsSet("WAYLAND_DISPLAY") && qEnvironmentVariableIsSet("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);
    app.setApplicationName("apprenticeCAM");
    QString shot, file, ui_dir, want_tab; int W = 1600, H = 1000;
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); i++) {
        if (args[i] == "--shot" && i + 1 < args.size()) { shot = args[++i]; view_anim_ms = 0; }   /* one frame: views jump */
        else if (args[i] == "--size" && i + 1 < args.size()) sscanf(args[++i].toUtf8().constData(), "%dx%d", &W, &H);
        else if (args[i] == "--tab" && i + 1 < args.size()) want_tab = args[++i];
        else if (args[i] == "--ui" && i + 1 < args.size()) ui_dir = args[++i];
        else if (args[i] == "--mcp") { }                 /* ours: the MCP control socket (mcpserver.cpp) */
        else file = args[i];
    }
    if (ui_dir.isEmpty()) {                              /* ui/ next to the executable, else ./ui */
        ui_dir = QString::fromUtf8(qc_exe_dir()) + "/ui";
        if (!QFile::exists(ui_dir + "/layout.txt")) ui_dir = "ui";
    }
    setUiDir(ui_dir);
    snprintf(cui_ui_dir, sizeof cui_ui_dir, "%s", ui_dir.toUtf8().constData());   /* core_ui: starter_tools_mm.tools (cui_toolpage.c) */
    UiLayout ui;
    if (!ui.load(ui_dir + "/layout.txt")) {
        fprintf(stderr, "cannot read %s/layout.txt (the menu layout in viewer/ui/; --ui DIR points elsewhere)\n", ui_dir.toUtf8().constData());
        return 1;
    }
    TextFit::install(&app);                              /* ours: text never clipped (textfit.cpp); its style first, then the palette */
    /* mcamui: Noto Sans 14 px (its nuklear font); the original's Segoe UI is a Windows font */
    if (QFontDatabase::hasFamily("Noto Sans")) { QFont f("Noto Sans"); f.setPixelSize(13); app.setFont(f); }
    {   /* greyed controls: text rgb(160,160,160) (mcamui.c rib_button, rib_grey), made readable on the desktop theme's own
           backgrounds (contrast.h: >= 3:1 against the disabled window / button / edit colour) */
        QPalette pal = app.palette();
        const std::pair<QPalette::ColorRole, QPalette::ColorRole> fb[] = {
            { QPalette::WindowText, QPalette::Window }, { QPalette::ButtonText, QPalette::Button }, { QPalette::Text, QPalette::Base } };
        for (auto [f, b] : fb) pal.setColor(QPalette::Disabled, f, readableOn(QColor(160, 160, 160), pal.color(QPalette::Disabled, b), 3.0));
        app.setPalette(pal);
    }
    if (!qEnvironmentVariableIsSet("MCAMUI_I_ACCEPT_THE_RISK") && !risk_agreement(shot)) return 0;

    qc_load_config();
    qc_recent_load();
    MainWindow w(ui);
    w.resize(W, H);
    w.show();
    QApplication::processEvents();
    if (!file.isEmpty()) w.openPath(file);
    w.startup();
    if (!want_tab.isEmpty()) w.setTabByName(want_tab);
    QString script = qEnvironmentVariable("MCAMUI_SCRIPT");
    QString events = qEnvironmentVariable("MCAMUI_EVENTS");
    if (!script.isEmpty()) {
        TestHooks::runScript(&w, script);
        if (shot.isEmpty() && !qEnvironmentVariableIsSet("MCAMUI_LIVE")) return 0;
    }
    /* mcamui's older test modes (phase 8): MCAMUI_CLICKTEST=N - Contour, then a click on entity N's midpoint (picking test);
       MCAMUI_TEST=c|p|d:E,E.. - Contour / Pocket / Drill on those entities, generated (tool 6) and posted */
    if (qEnvironmentVariableIsSet("MCAMUI_CLICKTEST") && loaded) {
        int en = qEnvironmentVariableIntValue("MCAMUI_CLICKTEST");
        if (en >= 0 && en < F.nents && F.ents[en].type) {
            const Poly *pp = &F.polys[F.ents[en].first_poly]; V3 q = F.pts[pp->first + pp->count / 2]; float sx, sy;
            w.galleryPick("Contour");
            QWidget *vw = w.view()->widget();
            if (scene_to_screen(vw->width(), vw->height(), q.x, q.y, q.z, &sx, &sy)) {
                if (ViewInput *in = viewInput(w.view())) in->setViewport();
                w.viewClick(sx, sy);
                QPoint g = vw->mapTo(&w, QPoint((int)sx, (int)sy));
                printf("click at (%d,%d) -> %d chain(s)", g.x(), g.y(), ncchains);
                for (int i = 0; i < ncchains; i++) { printf(" [%d ents%s:", cchains[i].n, cchains[i].closed ? " closed" : ""); for (int k = 0; k < cchains[i].n; k++) printf(" %d", cchains[i].ent[k]); printf("]"); }
                printf("\n");
            }
        }
    }
    if (qEnvironmentVariableIsSet("MCAMUI_TEST")) {
        QByteArray tb = qgetenv("MCAMUI_TEST"); const char *t = tb.constData(); int ty = t[0] == 'c' ? CAM_CONTOUR : t[0] == 'p' ? CAM_POCKET : CAM_DRILL;
        w.view()->withGL([&] {
            gallery_pick(cam_type_label(ty));
            for (const char *q = strchr(t, ':'); q && *q; q = strchr(q + 1, ',')) {
                int en = atoi(q + 1);
                if (ty == CAM_DRILL) { if (en < F.nents) { choles[ncholes].x = F.ents[en].c.x; choles[ncholes].y = F.ents[en].c.y; ncholes++; } }
                else if (cam_chain(&F, en, cfg_chain_tol, &cchains[ncchains])) ncchains++;   /* chain_tol (config 1001) */
            }
            open_params(); pending.p.tool_dia = 6; tool_choice = 0; generate_pending(); post_ops();
        });
        printf("%s\n", cam_msg); w.refreshAll();
    }
    if (!shot.isEmpty()) {                               /* a few ticks so the layout settles (and the test events ran) */
        int last = TestHooks::lastTick(events);
        for (int tick = 0; tick < (last + 3 > 3 ? last + 3 : 3); tick++) {
            if (!events.isEmpty()) TestHooks::pushEvents(&w, events, tick);
            QApplication::processEvents();
            w.view()->redraw();
            QApplication::processEvents();
        }
        if (!events.isEmpty()) { TestHooks::dumpState(&w, "events"); if (cui_point_pick_active() || geo_cmd) pp_dump(); if (show_stock_setup) st_dump(); }   /* the live AutoCursor of a point pick (tests) */
        TextFit::check("shot");
        return TestHooks::shot(&w, shot) ? 0 : 1;
    }
    McpServer::maybeStart(&w, args);                  /* ours: --mcp / APPRENTICECAM_MCP=1 (mcpserver.cpp) */
    return app.exec();
}
