/* rpx - lay out Mastercam's ActiveReports 3 report templates (.rpx, common/reports) over report data, as the
 * Mastercam report viewer shows them. The templates Mastercam ships use Detail sections only, with AR.Label,
 * AR.Field, AR.Line and AR.Subreport controls (all common/reports/MSC files, 2017); the NST templates (Geometry Nesting's
 * "Default Nesting Report" and its SR1..SR3) add AR.Shape, AR.Image, label borders, Visible="0" and labels bound to a
 * DataField - that is what this reads. */
#ifndef RPX_H
#define RPX_H

typedef struct RNode {        /* report data: an XML element (Mastercam writes these as XML for the viewer) */
    char name[32];
    char *text;
    struct RNode **kid; int nkid;
} RNode;
RNode *rnode_new(RNode *parent, const char *name, const char *text);
void rnode_free(RNode *n);

typedef struct {              /* one thing to draw, in twips from the page's printable top-left */
    int line;                 /* 0 text, 1 line, 2 image (an AR.Image's embedded picture: x, y, w, h its box) */
    const unsigned char *img; int nimg;   /* the image's file bytes (PNG / BMP ...), owned by the template cache */
    int sizemode;             /* AR.Image SizeMode: 0 clip, 1 stretch, 2 zoom (keep the aspect, fit the box) */
    int ls;                   /* a line's style: ActiveReports BorderLineStyle (0 / 1 solid, 2 dash, 3 dot, ...) */
    int page;
    double x, y, w, h;        /* text box, or line x1 y1 -> x2 y2 in x, y, w (= x2), h (= y2) */
    char *text;
    double pt; int bold, italic, align;   /* style: font size, align 0 left 1 centre 2 right */
} RItem;

typedef struct {
    RItem *it; int n, cap;
    int pages;
    double width;             /* PrintWidth of the top report (twips) */
    char err[200];
} RLayout;

/* lay out report file `rpx` (subreports are looked up next to it) for the records `rec` (the nodes the data source
   pattern selects); page = Letter, 1 inch margins (PageSettings empty = ActiveReports defaults) */
int rpx_layout(const char *rpx, RNode **rec, int nrec, RLayout *out);
void rpx_layout_free(RLayout *l);
/* 1 if the report or one of its subreports binds a subreport to data field `field` */
int rpx_binds(const char *rpx, const char *field);
#endif
