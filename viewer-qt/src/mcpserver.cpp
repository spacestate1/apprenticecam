// mcpserver - OURS (tooling for Claude Code / tests; no Mastercam behaviour here): a Unix-domain control socket that runs
// the MCAMUI_SCRIPT steps (TestHooks::runScript, unchanged) and the MCAMUI_EVENTS input of testhooks.cpp in the live
// app, on the UI thread, and answers with what the hooks print. The MCP bridge is tools/mcp_apprenticecam.py.
//
// Enabled only by `apprenticecam --mcp` or APPRENTICECAM_MCP=1. Socket: $APPRENTICECAM_MCP_SOCKET, else
// $XDG_RUNTIME_DIR/apprenticecam-<pid>.sock (fallback /tmp/apprenticecam-<uid>/, mode 0700) with mode 0600, plus the link
// apprenticecam.sock -> it for the latest instance (not made when APPRENTICECAM_MCP_NOLINK=1). APPRENTICECAM_MCP_NOANIM=1:
// view changes jump (as --shot runs).
//
// Protocol: one JSON object per line each way. Request {"id":X, "op":OP, ...}; reply {"id":X, "ok":bool, "error":TEXT,
// "state":STDOUT, "stderr":STDERR, "modal":{...}|null, "ms":N}. OPs:
//   steps {steps:"open:P;opdump", wait_ms}   the MCAMUI_SCRIPT steps (one state line per step), then events for wait_ms
//   events {events:"0:click:x,y;...", wait_ms}  MCAMUI_EVENTS input (ticks as the --shot loop of main.cpp), then a state line
//   type {text}                               the characters as key presses to the focused widget
//   wait {wait_ms}                            events for wait_ms (and until a toolpath computation has ended)
//   status | modal                            the app / the active modal window            (answered at once, even when busy)
//   dismiss_modal {button | path}             a button of the modal window by its text (accept / reject / default), or a
//                                             file dialog's file                            (at once)
//   screenshot {path, target: window|dialog}  TestHooks::shot (window + its dialogs) / the top dialog only  (at once)
//   quit {force}                              close the main window (force: leave the event loop, no prompts)
// The step requests run one at a time in arrival order; while a toolpath computation runs (g_regen_busy, the op dialog's
// OK on its worker thread) they wait. A step that opens a modal box (live sessions without MCAMUI_SCRIPT) answers when
// the box is closed: status / modal / dismiss_modal / screenshot are answered meanwhile.
#include "mcpserver.h"
#include "mainwindow.h"
#include "testhooks.h"
#include "viewwidget.h"
#include "opdlg.h"
#include <QApplication>
#include <QSocketNotifier>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QAbstractButton>
#include <QPixmap>
#include <deque>
#include <memory>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
extern "C" {
#include "scene.h"
}
#include "core_ui/core_ui.h"

namespace {

struct Client {
    int fd = -1;
    QSocketNotifier *rn = nullptr;
    QByteArray in;
    bool closed = false;
};
using ClientP = std::shared_ptr<Client>;

/* stdout / stderr of the hooks, per request: fds 1 and 2 go to temporary files */
struct Capture {
    FILE *f[2] = {}; int saved[2] = { -1, -1 }; bool on = false;
    void begin() {
        fflush(stdout); fflush(stderr);
        for (int k = 0; k < 2; k++) { f[k] = tmpfile(); if (!f[k]) { end(); return; } }
        for (int k = 0; k < 2; k++) { saved[k] = dup(k + 1); dup2(fileno(f[k]), k + 1); }
        on = true;
    }
    QString read(int k) {
        if (!f[k]) return QString();
        QByteArray b; char buf[65536]; size_t n; rewind(f[k]);
        while ((n = fread(buf, 1, sizeof buf, f[k])) > 0) b.append(buf, (int)n);
        return QString::fromUtf8(b);
    }
    void end(QString *out = nullptr, QString *err = nullptr) {
        fflush(stdout); fflush(stderr);
        for (int k = 0; k < 2; k++) if (saved[k] >= 0) { dup2(saved[k], k + 1); close(saved[k]); saved[k] = -1; }
        if (out) *out = read(0);
        if (err) *err = read(1);
        for (int k = 0; k < 2; k++) if (f[k]) { fclose(f[k]); f[k] = nullptr; }
        on = false;
    }
};

class Server : public QObject {
public:
    Server(MainWindow *w) : w_(w) {}
    bool listen();
    void shutdown();
private:
    void onAccept();
    void onRead(ClientP c);
    void drop(ClientP c);
    void reply(ClientP c, QJsonObject r);
    bool immediate(ClientP c, const QJsonObject &q);
    void pump();
    QJsonObject run(const QJsonObject &q);
    void waitIdle(int ms);
    QJsonValue modalInfo();
    QJsonObject status();
    QJsonObject dismissModal(const QJsonObject &q);
    QJsonObject screenshot(const QJsonObject &q);

    MainWindow *w_;
    int lfd_ = -1; QSocketNotifier *ln_ = nullptr;
    QString path_, link_;
    std::deque<std::pair<ClientP, QJsonObject>> queue_;
    bool busy_ = false;
    int shots_ = 0;
};

static QString runtimeDir() {
    QByteArray x = qgetenv("XDG_RUNTIME_DIR");
    if (!x.isEmpty() && QFileInfo(QString::fromLocal8Bit(x)).isDir()) return QString::fromLocal8Bit(x);
    QString d = QString("/tmp/apprenticecam-%1").arg(getuid());
    mkdir(d.toLocal8Bit().constData(), 0700);
    return d;
}

bool Server::listen() {
    QString dir = runtimeDir();
    path_ = qEnvironmentVariable("APPRENTICECAM_MCP_SOCKET");
    if (path_.isEmpty()) path_ = dir + QString("/apprenticecam-%1.sock").arg(getpid());
    QByteArray p = path_.toLocal8Bit();
    sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
    if ((size_t)p.size() >= sizeof a.sun_path) { fprintf(stderr, "mcp: socket path too long: %s\n", p.constData()); return false; }
    memcpy(a.sun_path, p.constData(), p.size());
    lfd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (lfd_ < 0) { perror("mcp: socket"); return false; }
    unlink(p.constData());
    mode_t um = umask(077);                            /* user only */
    int rc = bind(lfd_, (sockaddr *)&a, sizeof a);
    umask(um);
    if (rc < 0 || ::listen(lfd_, 8) < 0) { fprintf(stderr, "mcp: cannot listen on %s: %s\n", p.constData(), strerror(errno)); close(lfd_); lfd_ = -1; return false; }
    chmod(p.constData(), 0600);
    ln_ = new QSocketNotifier(lfd_, QSocketNotifier::Read, this);
    connect(ln_, &QSocketNotifier::activated, this, [this] { onAccept(); });
    if (qEnvironmentVariable("APPRENTICECAM_MCP_NOLINK") != "1" && qEnvironmentVariable("APPRENTICECAM_MCP_SOCKET").isEmpty()) {
        link_ = dir + "/apprenticecam.sock";
        QFile::remove(link_);
        if (!QFile::link(path_, link_)) link_.clear();
    }
    if (qEnvironmentVariable("APPRENTICECAM_MCP_NOANIM") == "1") view_anim_ms = 0;
    fprintf(stderr, "mcp: listening on %s\n", p.constData());
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { shutdown(); });
    return true;
}

void Server::shutdown() {
    if (lfd_ < 0) return;
    close(lfd_); lfd_ = -1;
    unlink(path_.toLocal8Bit().constData());
    if (!link_.isEmpty() && QFileInfo(link_).symLinkTarget() == QFileInfo(path_).absoluteFilePath()) QFile::remove(link_);
}

void Server::onAccept() {
    for (;;) {
        int fd = accept4(lfd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) return;
        auto c = std::make_shared<Client>(); c->fd = fd;
        c->rn = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(c->rn, &QSocketNotifier::activated, this, [this, c] { onRead(c); });
    }
}

void Server::drop(ClientP c) {
    if (c->closed) return;
    c->closed = true;
    if (c->rn) { c->rn->setEnabled(false); c->rn->disconnect(); c->rn->deleteLater(); c->rn = nullptr; }
    close(c->fd); c->fd = -1;
}

void Server::onRead(ClientP c) {
    char buf[65536];
    for (;;) {
        ssize_t n = recv(c->fd, buf, sizeof buf, 0);
        if (n > 0) { c->in.append(buf, (int)n); continue; }
        if (n == 0) { drop(c); break; }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) drop(c);
        break;
    }
    int nl;
    while ((nl = c->in.indexOf('\n')) >= 0) {
        QByteArray line = c->in.left(nl).trimmed(); c->in.remove(0, nl + 1);
        if (line.isEmpty()) continue;
        QJsonParseError pe; QJsonDocument d = QJsonDocument::fromJson(line, &pe);
        if (!d.isObject()) { QJsonObject r; r["ok"] = false; r["error"] = "bad JSON: " + pe.errorString(); reply(c, r); continue; }
        QJsonObject q = d.object();
        if (immediate(c, q)) continue;
        queue_.emplace_back(c, q);
    }
    if (!queue_.empty()) QTimer::singleShot(0, this, [this] { pump(); });
    if (c->closed) {   /* drop its queued requests */
        for (auto it = queue_.begin(); it != queue_.end();) it = it->first == c ? queue_.erase(it) : it + 1;
    }
}

void Server::reply(ClientP c, QJsonObject r) {
    if (c->closed) return;
    QByteArray b = QJsonDocument(r).toJson(QJsonDocument::Compact) + "\n";
    const char *p = b.constData(); size_t left = b.size();
    while (left > 0) {
        ssize_t n = send(c->fd, p, left, MSG_NOSIGNAL);
        if (n > 0) { p += n; left -= n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { pollfd pf = { c->fd, POLLOUT, 0 }; if (poll(&pf, 1, 5000) > 0) continue; }
        drop(c); return;
    }
}

/* answered at once (also while a step request runs or waits for a modal box) */
bool Server::immediate(ClientP c, const QJsonObject &q) {
    QString op = q.value("op").toString("steps");
    QJsonObject r;
    if (op == "status") r = status();
    else if (op == "modal") { r["ok"] = true; r["modal"] = modalInfo(); }
    else if (op == "dismiss_modal") r = dismissModal(q);
    else if (op == "screenshot") r = screenshot(q);
    else return false;
    if (q.contains("id")) r["id"] = q.value("id");
    reply(c, r);
    return true;
}

void Server::pump() {
    if (busy_ || queue_.empty()) return;
    if (g_regen_busy) { QTimer::singleShot(50, this, [this] { pump(); }); return; }   /* the op dialog's OK computes: wait */
    busy_ = true;
    auto [c, q] = queue_.front(); queue_.pop_front();
    QJsonObject r;
    if (!c->closed) r = run(q);
    if (q.contains("id")) r["id"] = q.value("id");
    reply(c, r);
    busy_ = false;
    if (!queue_.empty()) QTimer::singleShot(0, this, [this] { pump(); });
}

void Server::waitIdle(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms || g_regen_busy) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (t.elapsed() > ms) usleep(1000);
        if (t.elapsed() > 600000) break;              /* 10 minutes */
    }
    QApplication::processEvents();
}

QJsonObject Server::run(const QJsonObject &q) {
    QString op = q.value("op").toString("steps");
    QJsonObject r; r["ok"] = true;
    QElapsedTimer t; t.start();
    int wait = q.value("wait_ms").toInt(0);
    Capture cap; QString out, err;
    if (op == "steps") {
        QString s = q.value("steps").toString();
        cap.begin();
        if (!s.isEmpty()) TestHooks::runScript(w_, s);
        waitIdle(wait);
        cap.end(&out, &err);
    } else if (op == "events") {                       /* as main.cpp's --shot loop: one tick = events + a repaint */
        QString ev = q.value("events").toString();
        cap.begin();
        int last = TestHooks::lastTick(ev);
        for (int tick = 0; tick < last + 2; tick++) {
            TestHooks::pushEvents(w_, ev, tick);
            QApplication::processEvents(); w_->view()->redraw(); QApplication::processEvents();
        }
        waitIdle(wait);
        TestHooks::dumpState(w_, "events");
        cap.end(&out, &err);
    } else if (op == "type") {
        QString text = q.value("text").toString();
        cap.begin();
        for (QChar ch : text) {
            QWidget *tw = QApplication::focusWidget() ? QApplication::focusWidget() : w_->view()->widget();
            int key = ch == '\n' ? Qt::Key_Return : ch == '\t' ? Qt::Key_Tab : ch.toUpper().unicode();
            QString tx = ch == '\n' ? QString("\r") : QString(ch);
            QKeyEvent d(QEvent::KeyPress, key, Qt::NoModifier, tx), u(QEvent::KeyRelease, key, Qt::NoModifier, tx);
            QApplication::sendEvent(tw, &d); QApplication::sendEvent(tw, &u);
            QApplication::processEvents();
        }
        waitIdle(wait);
        TestHooks::dumpState(w_, "type");
        cap.end(&out, &err);
    } else if (op == "wait") {
        cap.begin(); waitIdle(wait); cap.end(&out, &err);
    } else if (op == "quit") {
        bool force = q.value("force").toBool();
        QTimer::singleShot(0, qApp, [this, force] { if (force) qApp->exit(0); else w_->close(); });
    } else {
        r["ok"] = false; r["error"] = "unknown op " + op;
    }
    r["state"] = out; r["stderr"] = err;
    r["modal"] = modalInfo();
    r["ms"] = (qint64)t.elapsed();
    return r;
}

static QString plain(QString s) { return s.remove('&'); }

QJsonValue Server::modalInfo() {
    QWidget *m = QApplication::activeModalWidget();
    if (!m) return QJsonValue::Null;
    QJsonObject o;
    o["class"] = m->metaObject()->className(); o["name"] = m->objectName(); o["title"] = m->windowTitle();
    if (auto *mb = qobject_cast<QMessageBox *>(m)) o["text"] = mb->text();
    if (auto *fd = qobject_cast<QFileDialog *>(m)) { o["file_dialog"] = true; o["directory"] = fd->directory().absolutePath(); o["filters"] = fd->nameFilters().join(";;"); }
    QJsonArray bs;
    for (QAbstractButton *b : m->findChildren<QAbstractButton *>()) if (b->isVisible() && !plain(b->text()).isEmpty()) bs.append(plain(b->text()));
    o["buttons"] = bs;
    return o;
}

QJsonObject Server::status() {
    QJsonObject r; r["ok"] = true;
    r["pid"] = (qint64)getpid(); r["socket"] = path_; r["cwd"] = QDir::currentPath();
    r["loaded"] = loaded != 0; r["part"] = loaded && F.path[0] ? QString::fromUtf8(F.path) : QString();
    r["project"] = doc_path[0] ? QString::fromUtf8(doc_path) : QString(); r["dirty"] = doc_dirty != 0;
    r["busy"] = busy_; r["queued"] = (int)queue_.size(); r["regen_busy"] = g_regen_busy;
    r["scripted"] = qEnvironmentVariableIsSet("MCAMUI_SCRIPT");     /* scripted semantics: no modal boxes, prompts stay open (answer:) */
    r["platform"] = QGuiApplication::platformName();
    r["title"] = w_->windowTitle(); r["tab"] = w_->currentTabName(); r["message"] = w_->lastMessage();
    QJsonArray wins;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl->isVisible() && tl->isWindow() && tl != w_ && tl->objectName() != "qtooltip_label") {
            QJsonObject o; o["class"] = tl->metaObject()->className(); o["name"] = tl->objectName(); o["title"] = tl->windowTitle(); wins.append(o); }
    r["windows"] = wins;
    r["modal"] = modalInfo();
    return r;
}

QJsonObject Server::dismissModal(const QJsonObject &q) {
    QJsonObject r; QWidget *m = QApplication::activeModalWidget();
    if (!m) { r["ok"] = false; r["error"] = "no modal window"; return r; }
    r["dismissed"] = modalInfo();
    QString want = q.value("button").toString(), path = q.value("path").toString();
    auto *dlg = qobject_cast<QDialog *>(m);
    if (auto *fd = qobject_cast<QFileDialog *>(m)) {
        if (!path.isEmpty() && want != "reject" && want != "cancel") { fd->selectFile(path); static_cast<QDialog *>(fd)->accept(); r["ok"] = true; return r; }
        fd->reject(); r["ok"] = true; return r;
    }
    if (!want.isEmpty() && want != "accept" && want != "reject" && want != "default") {
        for (QAbstractButton *b : m->findChildren<QAbstractButton *>())
            if (b->isVisible() && b->isEnabled() && plain(b->text()).compare(want, Qt::CaseInsensitive) == 0) { b->click(); r["ok"] = true; return r; }
        r["ok"] = false; r["error"] = "no button " + want; return r;
    }
    if (auto *mb = qobject_cast<QMessageBox *>(m); mb && (want.isEmpty() || want == "default")) {
        QAbstractButton *b = mb->defaultButton() ? (QAbstractButton *)mb->defaultButton() : mb->escapeButton();
        if (b) { b->click(); r["ok"] = true; return r; }
    }
    if (dlg) { if (want == "accept") dlg->accept(); else dlg->reject(); r["ok"] = true; return r; }
    m->close(); r["ok"] = true; return r;
}

QJsonObject Server::screenshot(const QJsonObject &q) {
    QJsonObject r;
    QString path = q.value("path").toString();
    if (path.isEmpty()) path = QFileInfo(path_).absolutePath() + QString("/apprenticecam-%1-shot-%2.png").arg(getpid()).arg(++shots_);
    QString target = q.value("target").toString("window");
    QApplication::processEvents(); w_->view()->redraw(); QApplication::processEvents();
    bool ok;
    if (target == "dialog") {                          /* the modal window, else the active window, else the last shown dialog */
        QWidget *d = QApplication::activeModalWidget();
        if (!d && QApplication::activeWindow() != w_) d = QApplication::activeWindow();
        if (!d) for (QWidget *tl : QApplication::topLevelWidgets()) if (tl != w_ && tl->isVisible() && tl->isWindow() && qobject_cast<QDialog *>(tl)) d = tl;
        if (!d) { r["ok"] = false; r["error"] = "no dialog shown"; return r; }
        ok = d->grab().save(path);
    } else {
        fflush(stdout);
        ok = TestHooks::shot(w_, path);              /* prints "wrote PATH" (into a running request's state, if any) */
    }
    r["ok"] = ok; r["path"] = path;
    if (!ok) r["error"] = "cannot write " + path;
    return r;
}

Server *g_server;
}

namespace McpServer {
bool maybeStart(MainWindow *w, const QStringList &args) {
    if (!args.contains("--mcp") && qEnvironmentVariable("APPRENTICECAM_MCP") != "1") return false;
    if (g_server) return true;
    g_server = new Server(w);
    if (!g_server->listen()) { delete g_server; g_server = nullptr; return false; }
    return true;
}
}
