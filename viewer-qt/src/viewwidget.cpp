#include <GL/glew.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "viewwidget.h"
#include <QBitmap>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <QPainter>
#include <QFontMetrics>
extern "C" {
#include "scene.h"
}
#include "core_ui/core_ui.h"
#include "core_bridge.h"

static bool g_glew_done;
static void glew_once() {
    if (g_glew_done) return;
    glewExperimental = GL_TRUE;
    GLenum r = glewInit();                             /* EGL / Wayland contexts: "No GLX display" after the GL entry
                                                          points were loaded (glewContextInit) - harmless */
    if (r != GLEW_OK && r != 4 /* GLEW_ERROR_NO_GLX_DISPLAY */) fprintf(stderr, "glewInit: %s\n", (const char *)glewGetErrorString(r));
    glGetError();
    gl_setup();
    g_glew_done = true;
}

/* ------------------------------------------------------------------------------------------------ overlay widget */
ViewOverlay::ViewOverlay(QWidget *parent, ViewInput *in) : QWidget(parent), in_(in) {
    setAttribute(Qt::WA_TransparentForMouseEvents); setAttribute(Qt::WA_NoSystemBackground); setAttribute(Qt::WA_TranslucentBackground);
    setGeometry(parent->rect());
}
void ViewOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    if (loaded && ncchains && chain_dlg_shown()) {
        /* chain direction arrows (core_ui chain_arrows: draw_chain_direction / draw_chain_end_direction -> DisplayChainArrow,
           GrArrow 0.1 x view height, head 13 x 1.07 px, shaft radius head / 5, colour palette 4 / 86 by the background,
           end arrows palette 12; drawn flat, as mcamui.c draw_cam "chainarrows") */
        vp_x = 0; vp_y = 0; vp_w = width(); vp_h = height();
        ChainArrow ar[512]; int na = chain_arrows(ar, 512);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < na; i++) {
            QColor c((int)(ar[i].rgb[0] * 255), (int)(ar[i].rgb[1] * 255), (int)(ar[i].rgb[2] * 255));
            p.setPen(QPen(c, ar[i].w, Qt::SolidLine, Qt::FlatCap)); p.drawLine(QPointF(ar[i].x0, ar[i].y0), QPointF(ar[i].x1, ar[i].y1));
            QPointF t[3] = { { ar[i].t[0], ar[i].t[1] }, { ar[i].t[2], ar[i].t[3] }, { ar[i].t[4], ar[i].t[5] } };
            p.setPen(Qt::NoPen); p.setBrush(c); p.drawPolygon(t, 3);
        }
        p.setBrush(Qt::NoBrush); p.setRenderHint(QPainter::Antialiasing, false);
    }
    /* the polygon / vector being sketched and the Window chaining rectangle: lines of 1.5 px in rgb(20, 70, 160) (mcamui
       "polyband" / "zoomwin"; this program's colour) */
    if (chain_sketching()) {
        p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(QColor(20, 70, 160), 1.5));
        for (int i = 0; i + 1 < poly_n; i++) p.drawLine(QPointF(poly_x[i], poly_y[i]), QPointF(poly_x[i + 1], poly_y[i + 1]));
        p.drawLine(QPointF(poly_x[poly_n - 1], poly_y[poly_n - 1]), QPointF(in_->cursor));
        p.setRenderHint(QPainter::Antialiasing, false);
    }
    if (in_->winDrag || (in_->nsWin && in_->winA != in_->winB)) { p.setPen(QPen(QColor(20, 70, 160), 1.5)); p.drawRect(QRect(in_->winA, in_->winB).normalized()); }
    if (in_->zoomDrag) { p.setPen(QPen(QColor(20, 70, 160), 1.5)); p.drawRect(QRect(in_->zoomA, in_->zoomB).normalized()); }   /* mcamui "zoomwin" */
    if (piv_on) {
        /* rotation pivot markers (CGraphicsView middle-button rotate, decomp/mastercam_viewmouse.c 510-516,
           mastercam_axisrot.c 133, mastercam_viewrot2.c 413; FUN_18022a1b0 glBitmaps in the auto-highlight colour):
           marker 0 (13 x 13, raster x-6 / y-6) at the pivot, marker 1 (29 x 27, x-15 / y-13) once an axis rotation has
           moved - core_ui PIV_BM0 / PIV_BM1, mcamui.c draw_pivot_markers */
        float c[3] = { 1, 1, 1 }; mc_rgb(cfg_autohl_color, c);
        QColor col((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255));
        int x0 = (int)piv_sx, y0 = (int)piv_sy;
        for (int r = 0; r < 13; r++) for (int k = 0; k < 13; k++) if (PIV_BM0[r][k] == '#') p.fillRect(x0 - 6 + k, y0 - 6 + r, 1, 1, col);
        if (piv_axis_on) for (int r = 0; r < 27; r++) for (int k = 0; k < 29; k++) if (PIV_BM1[r][k] == '#') p.fillRect(x0 - 15 + k, y0 - 13 + r, 1, 1, col);
    }
    if (loaded && gn_on) {
        /* Transform > Dynamic's texts (core_ui cui_xdyn.c gn_texts): the X / Y / Z labels (GrGnomon::DrawLabel: Tahoma, bold when the
           size is under 41, centred - the GrText alignment 4 [inferred]) and the drag read-out (GrEdit: black on white at the cursor +
           (24, -24)) */
        vp_x = 0; vp_y = 0; vp_w = width(); vp_h = height();
        GnText t[8]; int n = gn_texts(t, 8);
        p.setRenderHint(QPainter::TextAntialiasing);
        for (int i = 0; i < n; i++) {
            QFont f = font();
            if (!t[i].box) { f.setFamily("Tahoma"); f.setPixelSize(qMax(1, (int)lround(t[i].px))); f.setBold(t[i].bold != 0); }
            p.setFont(f); QFontMetrics fm(f); QString s = QString::fromUtf8(t[i].s);
            if (t[i].box) {
                QRect r(QPoint((int)t[i].x, (int)t[i].y - fm.height() - 2), QSize(fm.horizontalAdvance(s) + 6, fm.height() + 2));
                p.fillRect(r, Qt::white); p.setPen(Qt::black); p.drawText(r, Qt::AlignCenter, s);
            } else {
                p.setPen(QColor(t[i].rgba[0], t[i].rgba[1], t[i].rgba[2], t[i].rgba[3]));
                QRect r((int)t[i].x - 40, (int)t[i].y - 40, 80, 80); p.drawText(r, Qt::AlignCenter, s);
            }
        }
        p.setFont(font());
    }
    if (cam_msg[0] && (!show_params || lk_pick || pp_hide_on || tb_pick)) {   /* mcamui: not while the op dialog is up, except its suspended "..." pick / Hide dialog (batch 12) */
        /* prompt = mastercam.dll DisplayPrompt -> CPromptWnd (opmanres dialog 0x1ff / 0x201 / 0x202 by the registry
           "\GUI\PROMPT WND" font size; the user's registry has no such key -> 0x202): WS_POPUP | WS_BORDER, background
           default 0xffffff, size = longest line + 12 by lines x line height + 4 (FUN_180452890). Its default position is
           NOT DECODED: drawn at the top-left of the graphics area (mcamui.c draw_cam) */
        QStringList lines = QString::fromUtf8(cam_msg).split('\n');
        QFontMetrics fm(font()); int tw = 0;
        for (const QString &l : lines) tw = qMax(tw, fm.horizontalAdvance(l));
        int lh = fm.height() + 4, bw = tw + 12, bh = (int)lines.size() * lh + 4;
        p.fillRect(2, 2, bw, bh, Qt::white); p.setPen(Qt::black); p.drawRect(2, 2, bw, bh);
        for (int i = 0; i < lines.size(); i++) p.drawText(QRect(8, 4 + i * lh, bw - 6, lh), Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
    }
}

/* ------------------------------------------------------------------------------------------------ input */
ViewInput::ViewInput(QWidget *w, ViewApi *api) : QObject(w), w_(w), api_(api) {
    w->setMouseTracking(true);
    w->setFocusPolicy(Qt::StrongFocus);
    {   /* Mastercam's graphics-window cursor (gafres.dll CURSOR 4: a 17 px inverting cross, hotspot in the middle), generated
           (core_ui cui_cross_cursor; mcamui.c main loop): over the view; panels / dialogs keep the arrow */
        unsigned char data[128], mask[128]; cui_cross_cursor(data, mask);
        /* Qt: bitmap 1 + mask 0 = XOR (the X11 / Windows inverting pixel). Wayland (and the other platforms) cannot
           invert: there the cross is drawn black (bitmap 1 + mask 1) instead of leaving it undefined. */
        /* XWayland (xcb on a Wayland session - main.cpp picks it for UI-1) cannot invert either: its inverting pixels come out
           invisible, the cursor disappeared over the view (user report 2026-10-24) */
        bool xorOk = (QGuiApplication::platformName() == "xcb" && !qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) || QGuiApplication::platformName() == "windows";
        if (!xorOk) memcpy(mask, data, 128);
        QImage bi(32, 32, QImage::Format_Mono), mi(32, 32, QImage::Format_Mono);
        /* bit 1 = color1 (black) for QBitmap::fromImage: without a colour table every 0 bit converts to "set" and the
           cursor became a solid 32 x 32 box */
        QList<QRgb> ct{qRgb(255, 255, 255), qRgb(0, 0, 0)}; bi.setColorTable(ct); mi.setColorTable(ct);
        for (int y = 0; y < 32; y++) { memcpy(bi.scanLine(y), data + y * 4, 4); memcpy(mi.scanLine(y), mask + y * 4, 4); }
        w->setCursor(QCursor(QBitmap::fromImage(bi), QBitmap::fromImage(mi), 15, 15));
    }
    ov_ = new ViewOverlay(w, this);
    w->installEventFilter(this);
    // view transitions / zoom animations (config 33006 / 33007, scene.c view_anim_step): stepped while the view changes.
    // One clock for the process: every view input steps the one shared animation (two clocks made it run back and forth)
    static QElapsedTimer clock; if (!clock.isValid()) clock.start();
    connect(&anim_, &QTimer::timeout, this, [this] {
        ViewState a; view_get(&a);
        view_anim_step((double)clock.elapsed());
        ViewState b; view_get(&b);
        if (memcmp(&a, &b, sizeof a)) api_->redraw();
    });
    anim_.start(15);
}
bool ViewInput::eventFilter(QObject *o, QEvent *e) {
    if (o == w_ && e->type() == QEvent::Resize) ov_->setGeometry(w_->rect());
    return false;
}
/* the graphics viewport for core_ui's picking (window coordinates there = this widget's) */
void ViewInput::setViewport() { vp_x = 0; vp_y = 0; vp_w = w_->width(); vp_h = w_->height(); }

static ViewState vhist[64]; static int nvhist;
void ViewInput::viewPush() { if (nvhist == 64) { memmove(vhist, vhist + 1, sizeof(ViewState) * 63); nvhist--; } view_get(&vhist[nvhist++]); }
bool ViewInput::viewPop() { if (!nvhist) return false; view_put(&vhist[--nvhist]); return true; }

void ViewInput::press(QMouseEvent *e, bool dbl) {
    w_->setFocus();
    last_ = down_ = e->position().toPoint();
    setViewport();
    if (e->button() == Qt::LeftButton) dbl_ = dbl;
    if (e->button() == Qt::LeftButton && loaded && chain_window_drag()) {   /* Window chaining rectangle (mcamui zw_active 2) */
        winDrag = true; winA = winB = down_; ov_->update(); return;
    }
    if (e->button() == Qt::LeftButton && loaded && ns_deleting) { nsWin = true; winA = winB = down_; }   /* get_event mask 0x122: a drag
                                                           draws the window (FUN_1804e3b90 -> FUN_1804e6520), a click picks; the drag / click
                                                           threshold is mcamui's < 4 px test (Mastercam's: NOT DECODED) */
    if (e->button() == Qt::LeftButton && zoom_window) {     /* OnScreenZoom: the rectangle (mcamui zw_active 1) */
        zoomDrag = true; zw_active = 1; zoomA = zoomB = down_; ov_->update(); return;
    }
    if (e->button() == Qt::LeftButton && ex_mode && ex_phase && !ex_pick && loaded) {   /* sec. 1.3 event 0x400000: an extrude arrow picked -> start the drag (FUN_1800ddc70; mcamui main loop) */
        int kind; double A[3], D[3];
        if (ex_arrow_hit((float)down_.x(), (float)down_.y(), &kind, A, D)) { ex_drag_start(kind, A, D); drag_ = 0; view_wheel_run_end(); emit extrudeChanged(); return; }
    }
    if (loaded && xdyn_on() && (e->button() == Qt::LeftButton || e->button() == Qt::RightButton)) {
        /* Transform > Dynamic (core_ui cui_xform.c / cui_xdyn.c): the gnomon's get_event mouse cases - a handle picked (0x400000) starts
           its drag, the placement / a drag ends at the release (0x2000000), a double click applies (0x800), right click = Esc */
        float x = (float)down_.x(), y = (float)down_.y();
        if (e->button() == Qt::LeftButton) api_->withGL([&] { xdyn_press(x, y, dbl ? 1 : 0); });
        else if (!(e->modifiers() & Qt::ControlModifier)) api_->withGL([] { xdyn_rclick(); });
        /* Ctrl-right-click (event 0x40000 -> WM_CONTEXTMENU, the gnomon settings): NOT DECODED - nothing (the settings are on the
           panel's Advanced tab) */
        drag_ = 0; view_wheel_run_end(); ov_->update(); emit chainChanged(); return;
    }
    if (e->button() == Qt::LeftButton && geo_cmd == G_BBOX && loaded && bb_arrow_hit((float)down_.x(), (float)down_.y())) {   /* the push-pull arrow grabbed (0x400000, cui_bbox.c) */
        bb_drag_start(); drag_ = 0; view_wheel_run_end(); emit extrudeChanged(); return;
    }
    if (e->button() == Qt::LeftButton) { drag_ = 1; view_wheel_run_end(); }          /* OnLButtonDown clears +0x38d8 */
    else if (e->button() == Qt::MiddleButton) {
        drag_ = 2;
        int shift_down = (e->modifiers() & Qt::ShiftModifier) != 0;
        mbPan_ = shift_down != qc_use_shf_to_spin;                                    /* bVar6 = (Shift == useShfToSpin) -> rotate */
        if (!mbPan_) {                       /* pivot: the picked geometry under the cursor, else the screen centre at the
                                                mean displayed depth (core_ui cui_rotate_begin: pick_point_under, FUN_18013dbd0) */
            cui_rotate_begin((float)down_.x(), (float)down_.y(), pivot_, qc_dyn_free);
            ov_->update();
        }
    } else if (e->button() == Qt::RightButton) drag_ = 3;
}

void ViewInput::release(QMouseEvent *e) {
    QPoint p = e->position().toPoint();
    if (bb_dragging() && e->button() == Qt::LeftButton) { setViewport(); bb_drag_end(0); drag_ = 0; emit extrudeChanged(); return; }   /* the push-pull commit (FUN_180346a60) */
    if (loaded && xdyn_on() && e->button() == Qt::LeftButton) {   /* Dynamic: the placement / a drag ends (core_ui xdyn_release) */
        setViewport(); float x = (float)p.x(), y = (float)p.y();
        api_->withGL([&] { xdyn_release(x, y); });
        drag_ = 0; ov_->update(); emit chainChanged(); return;
    }
    if (ex_drag && e->button() == Qt::LeftButton) { ex_drag_end(0); drag_ = 0; emit extrudeChanged(); return; }   /* 0x1000000 / 0x2000000: commit the drag (FUN_1800ddab0) */
    if (winDrag && e->button() == Qt::LeftButton) {      /* window_select, then the start point prompt 4235 / the chains (core_ui) */
        winDrag = false; setViewport();
        api_->withGL([this] { chain_window_done((float)winA.x(), (float)winA.y(), (float)winB.x(), (float)winB.y()); });
        ov_->update(); emit chainChanged(); return;
    }
    if (zoomDrag && e->button() == Qt::LeftButton) {     /* the rectangle fills the view (core_ui zoom_window_done) */
        zoomDrag = false;
        zoom_window_done((float)zoomA.x(), (float)zoomA.y(), (float)zoomB.x(), (float)zoomB.y(), w_->width(), w_->height());
        drag_ = 0; ov_->update(); api_->redraw(); emit zoomed(); return;
    }
    bool still = abs(p.x() - down_.x()) < 4 && abs(p.y() - down_.y()) < 4;     /* mcamui: the < 4 px click test */
    if (nsWin && e->button() == Qt::LeftButton) {        /* Nesting Results Delete: the window event 0x20 (core_ui cui_nest.c ns_del_window) */
        nsWin = false; ov_->update();
        if (!still && loaded) { setViewport(); api_->withGL([this] { ns_del_window((float)winA.x(), (float)winA.y(), (float)winB.x(), (float)winB.y()); });
                                drag_ = 0; cui_view_drag = 0; emit chainChanged(); return; }
    }
    if (e->button() == Qt::MiddleButton) { piv_on = piv_axis_on = 0; ov_->update(); }   /* GrTemp_Remove at the end of the drag */
    /* a drawing command takes its pick where the button went down, however far the mouse moved before the release
       (OnLButtonDown FUN_1801ca2c0 posts 0x8030 with the press point, decomp/mastercam_viewmouse.c; mcamui.c main loop);
       everything else keeps the < 4 px click test */
    bool pp = pressPick && pressPick();
    if (drag_ == 1 && e->button() == Qt::LeftButton && (pp || still) && loaded) { setViewport(); QPoint c = pp ? down_ : p; last_clicks = dbl_ ? 2 : 1; emit clicked((float)c.x(), (float)c.y()); }
    if (drag_ == 3 && e->button() == Qt::RightButton && still) emit contextMenu(w_->mapToGlobal(p));   /* default.mtb right mouse button menu */
    drag_ = 0; cui_view_drag = 0;
}

void ViewInput::move(QMouseEvent *e) {
    QPoint p = e->position().toPoint();
    int dx = p.x() - last_.x(), dy = p.y() - last_.y(); last_ = p;
    cursor = p;
    if (winDrag || nsWin) { winB = p; ov_->update(); }
    if (zoomDrag) { zoomB = p; ov_->update(); }
    if (ex_drag) { setViewport(); ex_drag_move((float)p.x(), (float)p.y()); emit extrudeChanged(); }
    if (bb_dragging()) { setViewport(); bb_drag_move((float)p.x(), (float)p.y()); emit extrudeChanged(); }
    if (loaded && xdyn_on()) {                             /* Dynamic: the hover highlight / the drag (core_ui xdyn_move) */
        setViewport(); bool r = false; float x = (float)p.x(), y = (float)p.y();
        api_->withGL([&] { r = xdyn_move(x, y) != 0; });
        if (r) ov_->update();
    }   /* the push-pull preview (FUN_1803469a0) */   /* 0x800000: the live drag value (FUN_1800dd7c0) */
    if (chain_sketching()) ov_->update();
    int vh = w_->height();
    setViewport();
    cui_view_drag = drag_ == 2;                           /* a middle-button rotate / pan: no AutoCursor re-run (core_ui) */
    double world[3] = { 0, 0, 0 }, raw[3] = { 0, 0, 0 };   /* geo_snap needs the unrounded cursor (its own grid pass) */
    bool snapped = cui_cursor_world((float)p.x(), (float)p.y(), world, raw) != 0;   /* core_ui: grid plane + sel_grid_search */
    emit cursorWorld(world[0], world[1], world[2], true, snapped);
    if (cui_point_pick_active()) api_->redraw();          /* a point pick's live AutoCursor marker (core_ui pp_hover) */
    if (tb_pick == 2) { tb_move(raw[0], raw[1]); api_->redraw(); }   /* Contour Tabs position_tab: the tab follows the cursor */
    if (geo_cmd) {                                          /* the drawing cursor (core_ui geo_cursor_update: AutoCursor + angular) */
        geo_cursor_update(raw, (e->modifiers() & Qt::ControlModifier) != 0, snapped);
        api_->redraw();
    }
    /* auto-highlight (DrawAutoHighlight 0x18031f290 via mccore FUN_1804df930): not during view drags (gr_dynamic) */
    if (cui_hover_update((float)p.x(), (float)p.y(), !drag_ && hlActive && hlActive())) api_->redraw();
    /* CGraphicsView::OnMouseMove (decomp/mastercam_viewmouse.c): a middle-button drag rotates about the pivot
       (RotateObject: free mode config 33002 = y, else turntable); with Shift it pans with the cursor
       (config 33009 'Middle button/wheel' = useShfToSpin swaps the two). The left button never moves
       the view: it selects. */
    if (drag_ == 2 && (dx || dy)) {
        if (mbPan_) view_pan_px(dx, -dy);
        else if (qc_dyn_free) {                                 /* RotateObject: Ctrl / X / Y / Z hold one axis */
            int axm = holdAxis ? holdAxis : (e->modifiers() & Qt::ControlModifier) ? 1 : keyX_ ? 2 : keyY_ ? 3 : keyZ_ ? 4 : 0;
            cui_rotate_free_step(axm, p.x(), vh - p.y(), dx, -dy, pivot_);   /* core_ui (marker 1) */
            ov_->update();
        }
        else view_rotate_turntable(dx, -dy, pivot_);
        api_->redraw();
    }
}

void ViewInput::leave() {
    emit cursorWorld(0, 0, 0, false, false);
    if (hover_ent >= 0) { hover_ent = -1; overlay_dirty = 1; api_->redraw(); }   /* mcamui: off the view = no hover */
}

void ViewInput::wheel(QWheelEvent *e) {                   /* OnMouseWheel: notches of the zoom factor about the cursor */
    int n = e->angleDelta().y() / 120; if (qc_reverse_wheel) n = -n;
    QPointF p = e->position();
    double px, py; view_px_to_view((float)p.x(), (float)p.y(), &px, &py);
    if (n) { view_zoom_steps(n, px, py, 1); api_->redraw(); }
}

/* graphics-window view keys, FUN_1803222f0 (decomp/mastercam_keyrot2.c), one step per key-down message (Windows key
   repeat repeats it): Alt+arrows rotate by config 4029 (FUN_1803278f0); arrows pan (FUN_180325ea0); Page Up / Down zoom
   one notch about the centre (ZoomUnzoom FUN_18032a080); Home fits. Ctrl only limits the change to the active
   viewport (bActiveViewOnly), so it does the same here. */
void ViewInput::viewKey(int k, Qt::KeyboardModifiers mod) {
    int alt = (mod & Qt::AltModifier) != 0;
    int ax = k == Qt::Key_Left ? -1 : k == Qt::Key_Right ? 1 : 0, ay = k == Qt::Key_Down ? -1 : k == Qt::Key_Up ? 1 : 0;
    if ((ax || ay) && alt) { viewPush(); view_rotate_keys(ax, ay, qc_vk_rot); api_->redraw(); return; }   /* set_prev_gview */
    if (alt) return;
    if (ax || ay) view_pan_steps(ax, ay, qc_vk_pan);
    if (k == Qt::Key_PageUp) { double f = cfg_zoom_fac; cfg_zoom_fac = qc_vk_zoom; view_zoom_steps(1, vcx, vcy, 0); cfg_zoom_fac = f; }
    if (k == Qt::Key_PageDown) { double f = cfg_zoom_fac; cfg_zoom_fac = qc_vk_unzoom; view_zoom_steps(-1, vcx, vcy, 0); cfg_zoom_fac = f; }
    if (k == Qt::Key_Home) view_fit(1);
    api_->redraw();
}

bool ViewInput::key(QKeyEvent *e, bool down) {
    if (e->key() == Qt::Key_X) keyX_ = down; else if (e->key() == Qt::Key_Y) keyY_ = down; else if (e->key() == Qt::Key_Z) keyZ_ = down;
    if (!down) return false;
    int k = e->key();
    if (k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Up || k == Qt::Key_Down || k == Qt::Key_PageUp || k == Qt::Key_PageDown || k == Qt::Key_Home) {
        viewKey(k, e->modifiers()); return true;
    }
    return false;
}

/* ------------------------------------------------------------------------------------------------ QOpenGLWidget */
GLView::GLView(QWidget *parent) : QOpenGLWidget(parent) {
    QSurfaceFormat f; f.setVersion(3, 3); f.setProfile(QSurfaceFormat::CoreProfile); f.setDepthBufferSize(24); f.setSamples(4);   /* mcamui: MSAA 4 */
    setFormat(f);
    in_ = new ViewInput(this, this);
}
void GLView::withGL(const std::function<void()> &fn) {
    if (!ready_) { pending_.push_back(fn); return; }
    view_w = width(); view_h = height();             /* the view size before a part is opened and fitted */
    if (depth_++ == 0) makeCurrent();                /* nested calls (a command that starts a new part) keep the context */
    fn();
    if (--depth_ == 0) doneCurrent();
    update();
}
void GLView::initializeGL() {
    glew_once();
    ready_ = true;
    view_w = width(); view_h = height();
    for (auto &fn : pending_) fn();
    pending_.clear();
}
void GLView::paintGL() {
    view_w = width(); view_h = height();             /* graphics view size (window pixels) */
    qreal r = devicePixelRatioF();
    glClearColor(.93f, .94f, .96f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    vp_x = 0; vp_y = 0; vp_w = width(); vp_h = height();
    if (loaded) build_overlay();                     /* core_ui: selection / hover stripes, dash buffer (mcamui: every frame) */
    draw_scene(0, 0, (int)(width() * r), (int)(height() * r));
    in_->overlay()->update();
}
void GLView::mousePressEvent(QMouseEvent *e) { in_->press(e); }
void GLView::mouseDoubleClickEvent(QMouseEvent *e) { in_->press(e, true); }
void GLView::mouseReleaseEvent(QMouseEvent *e) { in_->release(e); }
void GLView::mouseMoveEvent(QMouseEvent *e) { in_->move(e); }
void GLView::wheelEvent(QWheelEvent *e) { in_->wheel(e); }
void GLView::keyPressEvent(QKeyEvent *e) { if (!in_->key(e, true)) QOpenGLWidget::keyPressEvent(e); }
void GLView::keyReleaseEvent(QKeyEvent *e) { if (!in_->key(e, false)) QOpenGLWidget::keyReleaseEvent(e); }
void GLView::leaveEvent(QEvent *) { in_->leave(); }

/* ------------------------------------------------------------------------------------------------ EGL (no window system) */
static EGLDisplay g_dpy = EGL_NO_DISPLAY; static EGLContext g_ctx = EGL_NO_CONTEXT;
static bool egl_init() {
    if (g_ctx != EGL_NO_CONTEXT) return true;
    auto gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    const char *ce = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (gpd && ce && strstr(ce, "EGL_MESA_platform_surfaceless")) g_dpy = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (g_dpy == EGL_NO_DISPLAY) return false;
    EGLint ma, mi;
    if (!eglInitialize(g_dpy, &ma, &mi)) { g_dpy = EGL_NO_DISPLAY; return false; }
    eglBindAPI(EGL_OPENGL_API);
    EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
    g_ctx = eglCreateContext(g_dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    if (g_ctx == EGL_NO_CONTEXT) return false;
    if (!eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx)) return false;
    glew_once();
    fprintf(stderr, "view: EGL surfaceless context (%s)\n", (const char *)glGetString(GL_RENDERER));
    return true;
}
bool EglView::available() { return egl_init(); }

EglView::EglView(QWidget *parent) : QWidget(parent) {
    ok_ = egl_init();
    in_ = new ViewInput(this, this);
    setAttribute(Qt::WA_OpaquePaintEvent);
}
EglView::~EglView() {}
void EglView::withGL(const std::function<void()> &fn) {
    if (!ok_) return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx);
    view_w = width(); view_h = height();             /* the view size before a part is opened and fitted */
    fn(); update();
}
QImage EglView::render() {
    if (!ok_ || width() <= 0 || height() <= 0) return QImage();
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx);
    qreal r = devicePixelRatioF(); int w = (int)(width() * r), h = (int)(height() * r);
    if (w != fw_ || h != fh_) {                        /* colour + depth renderbuffers (mcamui --shot FBO) */
        if (fbo_) { glDeleteFramebuffers(1, &fbo_); glDeleteRenderbuffers(2, rb_); }
        glGenFramebuffers(1, &fbo_); glBindFramebuffer(GL_FRAMEBUFFER, fbo_); glGenRenderbuffers(2, rb_);
        glBindRenderbuffer(GL_RENDERBUFFER, rb_[0]); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_[0]);
        glBindRenderbuffer(GL_RENDERBUFFER, rb_[1]); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb_[1]);
        fw_ = w; fh_ = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    view_w = width(); view_h = height();
    glViewport(0, 0, w, h);
    glClearColor(.93f, .94f, .96f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    vp_x = 0; vp_y = 0; vp_w = width(); vp_h = height();
    if (loaded) build_overlay();                     /* core_ui: selection / hover stripes, dash buffer (mcamui: every frame) */
    draw_scene(0, 0, w, h);
    QImage im(w, h, QImage::Format_RGBA8888);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, im.bits());
    im = im.mirrored(false, true);
    im.setDevicePixelRatio(r);
    return im;
}
QImage EglView::grabView() { return render(); }

/* ours (formats/simulator.md 6): the scene drawn into an off-screen framebuffer of another size - the Simulator window's
   viewport shares the one scene (scene.c globals, the main view's GL context). Call inside ViewApi::withGL. The view size
   globals (view_w / view_h, vp_*) and the bound framebuffer / viewport are restored afterwards. */
QImage renderSceneImage(int lw, int lh, qreal r) {
    static unsigned fbo = 0, rb[2] = { 0, 0 }; static int fw = 0, fh = 0;
    int w = (int)(lw * r), h = (int)(lh * r);
    if (w <= 0 || h <= 0) return QImage();
    GLint prevFbo = 0, prevVp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo); glGetIntegerv(GL_VIEWPORT, prevVp);
    if (w != fw || h != fh) {
        if (fbo) { glDeleteFramebuffers(1, &fbo); glDeleteRenderbuffers(2, rb); }
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo); glGenRenderbuffers(2, rb);
        glBindRenderbuffer(GL_RENDERBUFFER, rb[0]); glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
        glBindRenderbuffer(GL_RENDERBUFFER, rb[1]); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
        fw = w; fh = h;
    }
    int sv[6] = { view_w, view_h, vp_x, vp_y, vp_w, vp_h };
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    view_w = lw; view_h = lh;
    glViewport(0, 0, w, h);
    glClearColor(.93f, .94f, .96f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    vp_x = 0; vp_y = 0; vp_w = lw; vp_h = lh;
    draw_scene(0, 0, w, h);
    QImage im(w, h, QImage::Format_RGBA8888);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, im.bits());
    im = im.mirrored(false, true);
    im.setDevicePixelRatio(r);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo); glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    view_w = sv[0]; view_h = sv[1]; vp_x = sv[2]; vp_y = sv[3]; vp_w = sv[4]; vp_h = sv[5];
    return im;
}
void EglView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    QImage im = render();
    if (im.isNull()) { p.fillRect(rect(), QColor(237, 240, 245)); p.drawText(rect(), Qt::AlignCenter, "No OpenGL 3.3 context"); return; }
    p.drawImage(0, 0, im);
}
void EglView::mousePressEvent(QMouseEvent *e) { in_->press(e); }
void EglView::mouseDoubleClickEvent(QMouseEvent *e) { in_->press(e, true); }
void EglView::mouseReleaseEvent(QMouseEvent *e) { in_->release(e); }
void EglView::mouseMoveEvent(QMouseEvent *e) { in_->move(e); }
void EglView::wheelEvent(QWheelEvent *e) { in_->wheel(e); }
void EglView::keyPressEvent(QKeyEvent *e) { if (!in_->key(e, true)) QWidget::keyPressEvent(e); }
void EglView::keyReleaseEvent(QKeyEvent *e) { if (!in_->key(e, false)) QWidget::keyReleaseEvent(e); }
void EglView::leaveEvent(QEvent *) { in_->leave(); }

/* ------------------------------------------------------------------------------------------------ choice */
ViewApi *createView(QWidget *parent) {
    bool force_egl = qEnvironmentVariableIntValue("APPRENTICECAM_EGL") != 0;
    /* Wayland + NVIDIA: Qt's context fails (EGL_BAD_MATCH 3009) and a QOpenGLWidget there draws black; the probe keeps
       such systems on the EGL view (APPRENTICECAM_GL=1 forces QOpenGLWidget) */
    if (!force_egl && qEnvironmentVariableIntValue("APPRENTICECAM_GL")) return new GLView(parent);
    if (!force_egl) {
        QOpenGLContext probe; QSurfaceFormat f; f.setVersion(3, 3); f.setProfile(QSurfaceFormat::CoreProfile);
        probe.setFormat(f);
        if (probe.create() && probe.format().majorVersion() * 10 + probe.format().minorVersion() >= 33) return new GLView(parent);
    }
    return new EglView(parent);
}
ViewInput *viewInput(ViewApi *v) {
    if (auto g = dynamic_cast<GLView *>(v)) return g->input();
    if (auto e = dynamic_cast<EglView *>(v)) return e->input();
    return nullptr;
}
