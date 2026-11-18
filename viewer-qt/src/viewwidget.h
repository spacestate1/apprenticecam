// viewwidget - the graphics view: viewer/scene.c (OpenGL 3.3 core) drawn in a QOpenGLWidget, with the view mouse /
// keys of viewer/mcamui.c (CGraphicsView::OnMouseMove / OnMouseWheel / FUN_1803222f0, decomp/mastercam_viewmouse.c,
// mastercam_keyrot2.c). Where the platform has no OpenGL (QT_QPA_PLATFORM=offscreen for --shot tests) the same scene
// is drawn through a surfaceless EGL context into a framebuffer object and shown as an image (EglView).
#pragma once
#include <QWidget>
#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

class ViewInput;

// what the main window needs from either view
class ViewApi {
public:
    virtual ~ViewApi() = default;
    virtual void withGL(const std::function<void()> &fn) = 0;   // run with the scene's GL context current
    virtual void redraw() = 0;
    virtual QImage grabView() = 0;
    virtual QWidget *widget() = 0;
};

// the prompt box (mcamui.c draw_cam "prompt": CPromptWnd) and the rotation pivot markers (FUN_18022a1b0), drawn
// over the view by a transparent child widget (both kinds of view)
class ViewOverlay : public QWidget {
public:
    explicit ViewOverlay(QWidget *parent, ViewInput *in);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    ViewInput *in_;
};

// the shared input / animation logic (one per view)
class ViewInput : public QObject {
    Q_OBJECT
public:
    explicit ViewInput(QWidget *w, ViewApi *api);
    void setViewport();                                    // core_ui vp_x / vp_y / vp_w / vp_h = this view
    std::function<bool()> hlActive;                        // the auto-highlight rule now (core_ui cui_autohl_active)
    std::function<bool()> pressPick;                       // a pick takes the press point (a drawing command waits for a pick)
    ViewOverlay *overlay() { return ov_; }
    void press(QMouseEvent *e, bool dbl = false);       // dbl: the second press of a double click (SDL clicks = 2)
    void release(QMouseEvent *e);
    void move(QMouseEvent *e);
    void wheel(QWheelEvent *e);
    bool key(QKeyEvent *e, bool down);
    void leave();
    void viewKey(int key, Qt::KeyboardModifiers mod);      // mcamui.c view_key
    void viewPush();                                       // set_prev_gview (mcamui.c view_push)
    bool viewPop();
    int holdAxis = 0;                                      // test events: F:hold:N = Ctrl/X/Y/Z held (1-4)
    // Window chaining: the selection window being dragged (mcamui zw_active 2) and the cursor (polygon / vector band)
    bool winDrag = false; QPoint winA, winB, cursor;
    bool nsWin = false;                                    // Nesting Results Delete: a left drag = get_event's window (0x20), a click = the pick (2)
    bool zoomDrag = false; QPoint zoomA, zoomB;            // OnScreenZoom's rectangle being dragged (mcamui zw_active 1)
signals:
    void cursorWorld(double x, double y, double z, bool inView, bool snapped);
    void clicked(float x, float y);                        // a left click (< 4 px between press and release)
    void contextMenu(QPoint global);                       // a right click (< 4 px): the default.mtb menu
    void chainChanged();                                   // the Window chaining rectangle was released (core_ui chain_window_done)
    void extrudeChanged();                                 // an Extrude arrow drag started / moved / ended (core_ui ex_drag_*)
    void zoomed();                                         // the zoom window was released (core_ui zoom_window_done)
protected:
    bool eventFilter(QObject *o, QEvent *e) override;
private:
    QWidget *w_; ViewApi *api_; ViewOverlay *ov_ = nullptr;
    int drag_ = 0, mbPan_ = 0; bool dbl_ = false; double pivot_[3] = { 0, 0, 0 }; QPoint last_, down_;
    bool keyX_ = false, keyY_ = false, keyZ_ = false;
    QTimer anim_;
};

class GLView : public QOpenGLWidget, public ViewApi {
    Q_OBJECT
public:
    explicit GLView(QWidget *parent = nullptr);
    void withGL(const std::function<void()> &fn) override;
    void redraw() override { update(); }
    QImage grabView() override { return grabFramebuffer(); }
    QWidget *widget() override { return this; }
    ViewInput *input() { return in_; }
protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *) override;
private:
    ViewInput *in_; bool ready_ = false; std::vector<std::function<void()>> pending_; int depth_ = 0;
};

class EglView : public QWidget, public ViewApi {
    Q_OBJECT
public:
    explicit EglView(QWidget *parent = nullptr);
    ~EglView() override;
    static bool available();
    void withGL(const std::function<void()> &fn) override;
    void redraw() override { update(); }
    QImage grabView() override;
    QWidget *widget() override { return this; }
    ViewInput *input() { return in_; }
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *) override;
private:
    QImage render();
    ViewInput *in_; bool ok_ = false;
    unsigned fbo_ = 0, rb_[2] = { 0, 0 }; int fw_ = 0, fh_ = 0;
};

// the view for this platform: GLView when Qt can make an OpenGL 3.3 context, else EglView (or APPRENTICECAM_EGL=1)
ViewApi *createView(QWidget *parent);
ViewInput *viewInput(ViewApi *v);
// ours: the scene into an image of LW x LH logical pixels (inside ViewApi::withGL; the Simulator viewport, simwin.cpp)
QImage renderSceneImage(int lw, int lh, qreal dpr);
