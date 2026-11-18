/* draft_create_int.h - internals of viewer/draft_create.c shared with the other dimension ports (draft_create_*.c): the record byte
 * accessors, the calc context, the wl being built and the helpers ported once for calc_linear_dimension (each cites its Mastercam
 * function in draft_create.c). Not a public API: the names are mapped to dcx_* so they cannot clash in the viewer link. */
#ifndef DRAFT_CREATE_INT_H
#define DRAFT_CREATE_INT_H
#include "draft_create.h"
#include <stddef.h>
#include <string.h>

#include "mcam_load.h"
#define TOL (mc_systol.tol)                                         /* tol @1812b5b58 (config 1000, mcam_load.h mc_systol) */
#define TWOPI 6.283185307179586
#define DEG 57.29577951308233
#define RAD 0.01745329251994329
#define ANGTOL 5.7295779513082324e-05                              /* 0x180d324e0: degree tolerance (AbsAngleTheSame, FUN_180251290) */

static inline int16_t I16(const uint8_t *b, size_t o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static inline uint16_t U16(const uint8_t *b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static inline double D(const uint8_t *b, size_t o) { double v; memcpy(&v, b + o, 8); return v; }
static inline void PD(uint8_t *b, size_t o, double v) { memcpy(b + o, &v, 8); }
static inline void P16(uint8_t *b, size_t o, int v) { int16_t s = (int16_t)v; memcpy(b + o, &s, 2); }
static inline void P32(uint8_t *b, size_t o, uint32_t v) { memcpy(b + o, &v, 4); }

typedef struct { double x, y, z; } P3;
#define TXT 0x387
typedef struct { const DcSettings *S; const DcFonts *F; const DcInput *in; uint8_t fa[0x60]; const char *why;
                 char text[TXT]; } Ctx;    /* text = DAT_1813d5690, the one buffer both FUN_18030f410 calls write */
typedef struct { uint8_t b[0x98]; double pts[4][3]; } WL;          /* a v8_wit_ldr being built; pts = the DAT_1813c7a90.. arrays */

#define get_ext(...) dcx_get_ext(__VA_ARGS__)
#define fmt_num(...) dcx_fmt_num(__VA_ARGS__)
#define replace_v(...) dcx_replace_v(__VA_ARGS__)
#define f410(...) dcx_f410(__VA_ARGS__)
#define rot(...) dcx_rot(__VA_ARGS__)
#define rot_deg(...) dcx_rot_deg(__VA_ARGS__)
#define w2v(...) dcx_w2v(__VA_ARGS__)
#define abs_same(...) dcx_abs_same(__VA_ARGS__)
#define ang_rads(...) dcx_ang_rads(__VA_ARGS__)
#define l_ints(...) dcx_l_ints(__VA_ARGS__)
#define f254bc0(...) dcx_f254bc0(__VA_ARGS__)
#define box_limits(...) dcx_box_limits(__VA_ARGS__)
#define npts(...) dcx_npts(__VA_ARGS__)
#define make_wl_single(...) dcx_make_wl_single(__VA_ARGS__)
#define slot_font(...) dcx_slot_font(__VA_ARGS__)
#define placement(...) dcx_placement(__VA_ARGS__)
#define offsets(...) dcx_offsets(__VA_ARGS__)
int get_ext(const DcFonts *F, const char *s, const uint8_t *fa, double *ph, double *pw);
int fmt_num(char *out, size_t on, int fmt, int digits, double v, int lead, int trail, int comma);
void replace_v(const char *t, const char *v, char *out, size_t on);
int f410(Ctx *X, int kind, int sym, double value, char *text, double *w, double *h, double *maxw, double *toth, int *nstr);
void rot(double px, double py, double cx, double cy, double c, double s, double *ox, double *oy);
void rot_deg(P3 p, double deg, P3 *o);
void w2v(const double m[9], const double w[3], P3 *v);
int abs_same(double a, double b, int deg);
double ang_rads(double x0, double y0, double x1, double y1);
int l_ints(const double *L1, const double *L2, double *pt);
double f254bc0(int dir, double ref, double xa, double xb, const double *box, double y);
void box_limits(int mode, int nstr, double pad, const double *hs, const double *ws, double *bx, double *by);
int16_t *npts(WL *w);
void make_wl_single(WL *w, int kind, int has, int type, double h, double wd);
void slot_font(uint8_t *dst, const uint8_t *fa, double ang);
int placement(const Ctx *X, const uint8_t *E, double *pl);
void offsets(const Ctx *X, uint8_t *E, const P3 *P, double ang);
#endif
