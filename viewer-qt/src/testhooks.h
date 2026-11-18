// testhooks - offscreen tests without a desktop (viewer/mcamui.c --shot / MCAMUI_SCRIPT / MCAMUI_EVENTS):
//   QT_QPA_PLATFORM=offscreen MCAMUI_I_ACCEPT_THE_RISK=1 ./apprenticecam --shot out.png --size 1600x1000
//   MCAMUI_SCRIPT="open:PATH;view:7;shade:1;new;tab:View;mgr:0;selop:N;selops:1,3;opexpand:N;sel:3,4;ents;key:Ctrl+Z;
//                  screen:X,Y;save;saveas:P.mcamui;answer:save|discard;export:P.mcam;close;dump"
//   MCAMUI_EVENTS="F:click:x,y;F:clickw:wx,wy;F:rclick:x,y;F:move:x,y;F:down:x,y;F:up:x,y;F:mdown:x,y;F:mup:x,y;F:wheel:N,x,y;
//                  F:key:Ctrl+Z;F:hold:N" (window coordinates, at tick F; one tick = events processed + a repaint)
#pragma once
class MainWindow; class QString;
namespace TestHooks {
void runScript(MainWindow *w, const QString &script);
int lastTick(const QString &events);
void pushEvents(MainWindow *w, const QString &events, int tick);
void dumpState(MainWindow *w, const char *step);
bool shot(MainWindow *w, const QString &path);
}
