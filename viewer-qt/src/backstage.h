// backstage - the File tab (BCGP backstage view, IDR_BCGP_RIBBON_XML CATEGORY_BACKSTAGE; viewer/mcamui.c
// draw_backstage): the 15 entries in order (core_ui BS_ITEMS); the pages are the ribbonuires dialog templates (core_ui
// cui_misc.c BS_107 Info with the host callback's values, BS_105 Convert, BS_140 Print, BS_115 Help, BS_111 Community, the
// Open / Save As property sheets with BS_129 Recent Documents / BS_130 Computer), drawn as mcamui draws them: New, Open
// (recent documents + Browse), Save, Save As (Browse) and Help > About live, everything else greyed.
#pragma once
#include <QWidget>
class QPushButton;

class BackstagePage : public QWidget {                  // the page area: one template page (or the Open / Save As sheets)
    Q_OBJECT
public:
    explicit BackstagePage(QWidget *parent) : QWidget(parent) { setMouseTracking(true); }
    int item = 2, sub = 0;                             // BS_ITEMS entry, the property sheet page (Open: 0 Recent Documents, 1 Computer)
signals:
    void hit(int id);                                  // a live button (1099 About, 1127 Browse)
    void recent(const QString &path);                  // a recent document
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
private:
    QPoint mouse_;
};

class Backstage : public QWidget {
    Q_OBJECT
public:
    explicit Backstage(QWidget *parent = nullptr);
    void refresh();                                    // the recent documents / the Info values
    void showPage(int item);
    bool itemEnabled(int i) const;                     // the parity check
    bool pageButtonEnabled(int page, unsigned id) const;
signals:
    void back();
    void newPart();
    void openDialog();
    void openPath(const QString &path);
    void save();
    void saveAs();
    void about();                                      // Help > About apprenticeCAM (OnAppAbout)
    void configuration();                              // Configuration (the Current: configuration / units, formats/units.md)
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QWidget *left_; BackstagePage *page_; QList<QPushButton *> items_;
};
