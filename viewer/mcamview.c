/*
 * mcamview - show the geometry and stored toolpaths of a Mastercam 2017 / X9 part file (.mcam, .mcx-9).
 *
 * Built only from what was reverse engineered from the Mastercam DLLs (see ../formats/mcam.md):
 *   - file = stream of [u32 len][entity record]; some system records are followed by out-of-band blocks
 *     (toolpath groups, material table, Parasolid solids) that must be skipped  (mccore FUN_1802c62d0)
 *   - entity header: +0x00 u32 class, +0x06 u8 color, +0x08 i32 level, +0x2c i32 assoc subtype, body at +0x68
 *     P_ID(1): xyz   L_ID(2): xyz,xyz   A_ID(4): start,end,center xyz, r, start angle, sweep (radians)
 *     ASSOC_ID(0x4000) subtype TL_ID(3): tool struct at +0x68 (i32 number, i32 type, f64 diameter)
 *   - after the last record: binary NCI ([u16 size][nci_bin]) (mccore bnci_read_from_disk / bnci_to_string)
 *     -2 op start (+0x1c op id), 0/1 move (+0x20 xyz, +0x38 feed, feed<0 = rapid),
 *     2/3 arc (+0x18 plane, +0x20 end, +0x38 center, both plane-local u,v,depth), 1000-1002 tool change
 *     (+0x24 tool number, +0x50 first position), 81 drill point (+0x20 x,y,depth, +0x68 R plane)
 *
 * Build:  make            (needs SDL2)
 * Run:    ./mcamview PART.mcx-9            interactive window
 *         ./mcamview PART.mcx-9 --shot out.bmp [--iso] [--op N]   render one frame to a BMP and exit
 *
 * Mouse: left drag = rotate, right/middle drag = pan, wheel = zoom.
 * Keys:  T top view, I iso view, F front view, Z fit, G geometry on/off, P toolpath on/off, R rapids on/off,
 *        [ / ] previous / next operation (0 = all), SPACE play/pause toolpath simulation, +/- sim speed,
 *        BACKSPACE restart simulation, ESC / Q quit.
 */
#include "platform.h"
#include "mcam_grp.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------ data */

typedef struct { double x, y, z; } V3;

enum { K_GEOM, K_CUT, K_RAPID };

typedef struct {          /* one drawable polyline */
    int kind;             /* K_GEOM / K_CUT / K_RAPID */
    int op;               /* operation id (toolpaths), 0 for geometry */
    int tool;             /* tool number (toolpaths) */
    uint8_t color;        /* Mastercam color index (geometry) */
    int first, count;     /* range in the point array */
} Poly;

static V3 *pts; static int npts, cappts;
static Poly *polys; static int npolys, cappolys;
static int ops[1024], nops;                 /* op ids in NCI order */
static struct { int num; double dia; } tools[256]; static int ntools;
static long nlines, narcs, npoints_ent, nmoves, nnciarcs, ndrill, arc_fixups;

static void addpt(V3 p) {
    if (npts == cappts) { cappts = cappts ? cappts * 2 : 4096; pts = realloc(pts, cappts * sizeof(V3)); }
    pts[npts++] = p;
}
static Poly *newpoly(int kind, int op, int tool, uint8_t color) {
    if (npolys == cappolys) { cappolys = cappolys ? cappolys * 2 : 1024; polys = realloc(polys, cappolys * sizeof(Poly)); }
    Poly *p = &polys[npolys++];
    p->kind = kind; p->op = op; p->tool = tool; p->color = color; p->first = npts; p->count = 0;
    return p;
}
static void polypt(Poly *p, V3 v) { addpt(v); p->count++; }

static uint32_t rd32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t rd16(const uint8_t *b) { return b[0] | b[1] << 8; }
static uint64_t rd64(const uint8_t *b) { return rd32(b) | (uint64_t)rd32(b + 4) << 32; }
static double rdd(const uint8_t *b) { double d; memcpy(&d, b, 8); return d; }
static V3 rdv(const uint8_t *b) { V3 v = { rdd(b), rdd(b + 8), rdd(b + 16) }; return v; }

/* ------------------------------------------------------------------------------------------------ entities */

static void add_arc(int kind, int op, int tool, uint8_t color, V3 c, double r, double a0, double sw, int ax_u, int ax_v,
                    int ax_d, double d0, double d1) {
    /* points on an arc in the (u,v) plane of world axes ax_u/ax_v, depth interpolated d0->d1 (helix) */
    int n = (int)(fabs(sw) * 180 / M_PI / 3) + 2;
    if (n > 720) n = 720;
    Poly *p = newpoly(kind, op, tool, color);
    for (int i = 0; i <= n; i++) {
        double a = a0 + sw * i / n, q[3];
        q[ax_u] = (&c.x)[ax_u] + r * cos(a);
        q[ax_v] = (&c.x)[ax_v] + r * sin(a);
        q[ax_d] = d0 + (d1 - d0) * i / n;
        V3 v = { q[0], q[1], q[2] };
        polypt(p, v);
    }
}

static void entity(const uint8_t *rec, uint32_t n) {
    if (n < 0x68) return;
    uint32_t cls = rd32(rec);
    uint8_t color = rec[6];
    if (cls == 0x1 && n >= 0x68 + 24) {                 /* point: small cross */
        V3 p = rdv(rec + 0x68);
        Poly *a = newpoly(K_GEOM, 0, 0, color);
        V3 p1 = { p.x - 0.5, p.y, p.z }, p2 = { p.x + 0.5, p.y, p.z };
        polypt(a, p1); polypt(a, p2);
        Poly *b = newpoly(K_GEOM, 0, 0, color);
        V3 p3 = { p.x, p.y - 0.5, p.z }, p4 = { p.x, p.y + 0.5, p.z };
        polypt(b, p3); polypt(b, p4);
        npoints_ent++;
    } else if (cls == 0x2 && n >= 0x68 + 48) {          /* line */
        Poly *p = newpoly(K_GEOM, 0, 0, color);
        polypt(p, rdv(rec + 0x68)); polypt(p, rdv(rec + 0x80));
        nlines++;
    } else if (cls == 0x4 && n >= 0x68 + 96) {          /* arc */
        V3 s = rdv(rec + 0x68), e = rdv(rec + 0x80), c = rdv(rec + 0x98);
        double r = rdd(rec + 0xb0), a0 = rdd(rec + 0xb8), sw = rdd(rec + 0xc0);
        /* arcs are stored in their construction plane; the start/end points are world. If the XY-plane
           interpretation does not reproduce them (arc drawn in another view), fall back to a 3-point arc. */
        double sx = c.x + r * cos(a0), sy = c.y + r * sin(a0);
        if (fabs(sx - s.x) < 1e-4 * (1 + r) && fabs(sy - s.y) < 1e-4 * (1 + r) && fabs(s.z - e.z) < 1e-6) {
            add_arc(K_GEOM, 0, 0, color, c, r, a0, sw, 0, 1, 2, c.z, c.z);
        } else {                                        /* other plane: circle through s, e around c */
            V3 u = { s.x - c.x, s.y - c.y, s.z - c.z }, w = { e.x - c.x, e.y - c.y, e.z - c.z };
            V3 nrm = { u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x };
            double nl = sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
            Poly *p = newpoly(K_GEOM, 0, 0, color);
            if (nl < 1e-12) { polypt(p, s); polypt(p, e); }
            else {
                nrm.x /= nl; nrm.y /= nl; nrm.z /= nl;
                V3 v = { nrm.y * u.z - nrm.z * u.y, nrm.z * u.x - nrm.x * u.z, nrm.x * u.y - nrm.y * u.x };
                double asw = fabs(sw);
                int k = (int)(asw * 180 / M_PI / 3) + 2;
                for (int i = 0; i <= k; i++) {
                    double a = asw * i / k;
                    V3 q = { c.x + u.x * cos(a) + v.x * sin(a), c.y + u.y * cos(a) + v.y * sin(a), c.z + u.z * cos(a) + v.z * sin(a) };
                    polypt(p, q);
                }
            }
            arc_fixups++;
        }
        narcs++;
    } else if (cls == 0x4000 && n >= 0x68 + 0x10 && (int32_t)rd32(rec + 0x2c) == 3 && ntools < 256) {   /* TL_ID */
        tools[ntools].num = (int32_t)rd32(rec + 0x68);
        tools[ntools].dia = rdd(rec + 0x70);
        ntools++;
    }
}

/* ------------------------------------------------------------------------------------------------ NCI */

static void nci(const uint8_t *d, size_t len, size_t off) {
    static uint8_t buf[0x370];
    V3 cur = { 0, 0, 0 };
    int op = 0, tool = 0;
    Poly *line = NULL; int linekind = -1;
    while (off + 2 <= len) {
        uint16_t sz = rd16(d + off);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        memset(buf, 0, sizeof buf);                      /* bnci_read_from_disk zero-fills a 0x370 nci_bin */
        memcpy(buf, d + off + 2, sz < sizeof buf ? sz : sizeof buf);
        off += 2 + sz;
        int16_t g = (int16_t)rd16(buf);
        if (g == -2) {
            op = (int32_t)rd32(buf + 0x1c);
            if (nops < 1024) ops[nops++] = op;
            line = NULL;
        } else if (g >= 1000 && g <= 1002) {
            tool = (int32_t)rd32(buf + 0x24);
            cur = rdv(buf + 0x50);
            line = NULL;
        } else if (g == 0 || g == 1) {
            V3 p = rdv(buf + 0x20);
            int kind = rdd(buf + 0x38) < 0 ? K_RAPID : K_CUT;
            if (!line || linekind != kind) {
                line = newpoly(kind, op, tool, 0); linekind = kind;
                polypt(line, cur);
            }
            polypt(line, p);
            cur = p; nmoves++;
        } else if (g == 2 || g == 3) {
            static const int AX[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };   /* plane -> world axes of (u, v) */
            int pl = (int16_t)rd16(buf + 0x18);
            if (pl < 0 || pl > 2) pl = 0;
            int au = AX[pl][0], av = AX[pl][1], ad = 3 - au - av;
            V3 e = rdv(buf + 0x20), c = rdv(buf + 0x38);
            double end[3], cen[3], st[3] = { cur.x, cur.y, cur.z };
            end[au] = e.x; end[av] = e.y; end[ad] = e.z;
            cen[au] = c.x; cen[av] = c.y; cen[ad] = st[ad];
            double r = hypot(st[au] - cen[au], st[av] - cen[av]);
            double a0 = atan2(st[av] - cen[av], st[au] - cen[au]), a1 = atan2(end[av] - cen[av], end[au] - cen[au]);
            double sw = g == 3 ? fmod(a1 - a0 + 4 * M_PI, 2 * M_PI) : -fmod(a0 - a1 + 4 * M_PI, 2 * M_PI);
            if (fabs(sw) < 1e-9) sw = g == 3 ? 2 * M_PI : -2 * M_PI;   /* full circle */
            V3 cv = { cen[0], cen[1], cen[2] };
            add_arc(K_CUT, op, tool, 0, cv, r, a0, sw, au, av, ad, st[ad], end[ad]);
            cur.x = end[0]; cur.y = end[1]; cur.z = end[2];
            line = NULL; nnciarcs++; nmoves++;
        } else if (g == 81) {                            /* drill point: plunge to depth and back to R plane */
            double x = rdd(buf + 0x20), y = rdd(buf + 0x28), z = rdd(buf + 0x30), rp = rdd(buf + 0x68);
            Poly *r1 = newpoly(K_RAPID, op, tool, 0);
            V3 a = { x, y, cur.z }, b = { x, y, rp }, c = { x, y, z };
            polypt(r1, cur); polypt(r1, a); polypt(r1, b);
            Poly *p = newpoly(K_CUT, op, tool, 0);
            polypt(p, b); polypt(p, c); polypt(p, b);
            cur = b; line = NULL; ndrill++;
        }
    }
}

static int load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    fseek(f, 0, SEEK_END); size_t len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(len);
    if (fread(d, 1, len, f) != len) { fclose(f); return 0; }
    fclose(f);
    if (mcam_part_newer(d, len)) { fprintf(stderr, "%s\n", MCAM_NEWER_MSG); free(d); return 0; }   /* a newer release's part (mcam_grp.h) */
    size_t off = 0;
    while (off + 4 <= len) {
        uint32_t n = rd32(d + off);
        if (n == 0 || off + 4 + n > len) { nci(d, len, off); break; }   /* binary NCI tail */
        const uint8_t *rec = d + off + 4;
        entity(rec, n);
        off += 4 + n;
        /* out-of-band blocks after certain records (mcam_walk.py) */
        if (n >= 0x6a && rd32(rec) == 0x200) {
            uint16_t sub = rd16(rec + 0x68);
            if (sub == 0xF && off + 4 <= len) {                 /* toolpath groups */
                size_t gl = mcam_grp_block(d + off, len - off, NULL, 0, NULL); off = gl ? off + gl : len;   /* + member entries (mcam_grp.h) */
            } else if (sub == 0xE) {                            /* material table */
                while (off + 0x28 <= len) {
                    int32_t id = (int32_t)rd32(d + off); uint64_t s = rd64(d + off + 0x10);
                    off += 0x28;
                    if (id == 0) break;
                    off += s;
                }
            }
        } else if (n >= 0x16c && rd32(rec) == 0x20000) {        /* Parasolid solid + ops block */
            uint8_t ver = rec[0x49];
            off += rd32(rec + 0x168);
            off += 4 + (ver > 0x1b ? 0x100 : 0x40);
            off += 1 + 1 + 4 + 4 + 8;
            if (off + 4 > len) break;
            uint32_t cnt = rd32(d + off); off += 4;
            for (uint32_t i = 0; i < cnt && off + 8 <= len; i++) off += 8 + rd32(d + off + 4);
        }
    }
    free(d);
    return 1;
}

static double tool_dia(int num) {
    for (int i = 0; i < ntools; i++) if (tools[i].num == num) return tools[i].dia;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ view */

static int W = 1280, H = 860;
static double yaw = 0, pitch = 0, scale = 1, panx = 0, pany = 0;
static V3 ctr;
static int show_geom = 1, show_tp = 1, show_rapid = 1, sel = 0;   /* sel: 0 = all ops, k = ops[k-1] */
static long sim_n = -1; static int playing = 0; static double sim_speed = 200;  /* points per second */

static void project(V3 p, float *sx, float *sy) {
    double x = p.x - ctr.x, y = p.y - ctr.y, z = p.z - ctr.z;
    double cy = cos(yaw), sy_ = sin(yaw), cp = cos(pitch), sp = sin(pitch);
    double x1 = x * cy - y * sy_, y1 = x * sy_ + y * cy;          /* yaw about Z */
    double y2 = y1 * cp + z * sp;                                 /* pitch about screen X (+Z up) */
    *sx = (float)(W * 0.5 + panx + x1 * scale);
    *sy = (float)(H * 0.5 + pany - y2 * scale);
}

static void fit(void) {
    V3 lo = { 1e300, 1e300, 1e300 }, hi = { -1e300, -1e300, -1e300 };
    for (int i = 0; i < npts; i++) {
        V3 p = pts[i];
        if (fabs(p.x) > 1e6 || fabs(p.y) > 1e6 || fabs(p.z) > 1e6) continue;
        if (p.x < lo.x) lo.x = p.x; if (p.y < lo.y) lo.y = p.y; if (p.z < lo.z) lo.z = p.z;
        if (p.x > hi.x) hi.x = p.x; if (p.y > hi.y) hi.y = p.y; if (p.z > hi.z) hi.z = p.z;
    }
    if (lo.x > hi.x) { lo.x = lo.y = lo.z = -1; hi.x = hi.y = hi.z = 1; }
    ctr.x = (lo.x + hi.x) / 2; ctr.y = (lo.y + hi.y) / 2; ctr.z = (lo.z + hi.z) / 2;
    double ext = fmax(fmax(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);
    scale = 0.85 * fmin(W, H) / (ext > 0 ? ext : 1);
    panx = pany = 0;
}

/* Mastercam's 256-colour palette: mastercam.dll @0x8a6440 (4-byte RGB entries), ../formats/mastercam_palette.tsv
   (the same table as scene.c MC_PALETTE; this viewer is built from this one file) */
static const uint8_t PAL[256][3] = {
    { 0, 0, 0 }, { 0, 0, 128 }, { 0, 128, 0 }, { 0, 192, 192 }, { 128, 0, 0 }, { 128, 0, 128 }, { 128, 66, 0 }, { 192, 192, 192 },
    { 128, 128, 128 }, { 0, 0, 255 }, { 0, 255, 0 }, { 0, 255, 255 }, { 255, 0, 0 }, { 255, 0, 255 }, { 255, 255, 0 }, { 255, 255, 255 },
    { 255, 255, 204 }, { 255, 255, 153 }, { 255, 255, 128 }, { 255, 255, 102 }, { 255, 255, 51 }, { 204, 255, 255 }, { 204, 255, 204 }, { 204, 255, 153 },
    { 204, 255, 102 }, { 204, 255, 51 }, { 204, 255, 0 }, { 153, 255, 255 }, { 153, 255, 204 }, { 153, 255, 153 }, { 153, 255, 102 }, { 153, 255, 51 },
    { 153, 255, 0 }, { 102, 255, 255 }, { 102, 255, 204 }, { 102, 255, 153 }, { 102, 255, 102 }, { 102, 255, 51 }, { 102, 255, 0 }, { 51, 255, 255 },
    { 51, 255, 204 }, { 51, 255, 153 }, { 128, 255, 128 }, { 51, 255, 102 }, { 51, 255, 51 }, { 51, 255, 0 }, { 0, 255, 204 }, { 0, 255, 153 },
    { 0, 255, 102 }, { 0, 255, 51 }, { 255, 204, 255 }, { 255, 204, 204 }, { 255, 204, 153 }, { 255, 204, 102 }, { 255, 204, 51 }, { 255, 204, 0 },
    { 204, 204, 255 }, { 204, 204, 204 }, { 204, 204, 153 }, { 204, 204, 102 }, { 204, 204, 51 }, { 204, 204, 0 }, { 192, 192, 0 }, { 153, 204, 255 },
    { 153, 204, 204 }, { 153, 204, 153 }, { 153, 204, 102 }, { 153, 204, 51 }, { 153, 204, 0 }, { 102, 204, 255 }, { 102, 204, 204 }, { 102, 204, 153 },
    { 102, 204, 102 }, { 102, 204, 51 }, { 102, 204, 0 }, { 51, 204, 255 }, { 51, 204, 204 }, { 51, 204, 153 }, { 51, 204, 102 }, { 51, 204, 51 },
    { 51, 204, 0 }, { 0, 204, 255 }, { 0, 204, 204 }, { 0, 204, 153 }, { 0, 204, 102 }, { 0, 204, 51 }, { 0, 204, 0 }, { 0, 192, 0 },
    { 0, 170, 0 }, { 255, 153, 255 }, { 255, 153, 204 }, { 255, 153, 153 }, { 255, 153, 102 }, { 255, 153, 51 }, { 255, 153, 0 }, { 204, 153, 255 },
    { 204, 153, 204 }, { 204, 153, 153 }, { 204, 153, 102 }, { 204, 153, 51 }, { 204, 153, 0 }, { 153, 153, 255 }, { 153, 153, 204 }, { 153, 153, 153 },
    { 153, 153, 102 }, { 153, 153, 51 }, { 153, 153, 0 }, { 128, 128, 0 }, { 102, 153, 255 }, { 102, 153, 204 }, { 0, 128, 192 }, { 150, 160, 170 },
    { 0, 128, 128 }, { 102, 153, 102 }, { 102, 153, 51 }, { 102, 153, 0 }, { 51, 153, 255 }, { 51, 153, 204 }, { 51, 153, 153 }, { 51, 153, 102 },
    { 51, 153, 51 }, { 51, 153, 0 }, { 0, 153, 255 }, { 0, 128, 64 }, { 0, 153, 204 }, { 0, 153, 153 }, { 0, 153, 102 }, { 0, 153, 51 },
    { 0, 153, 0 }, { 255, 128, 255 }, { 255, 102, 255 }, { 255, 128, 192 }, { 255, 102, 204 }, { 255, 102, 153 }, { 255, 128, 128 }, { 255, 102, 102 },
    { 255, 128, 64 }, { 255, 102, 51 }, { 255, 102, 0 }, { 204, 102, 255 }, { 204, 102, 204 }, { 204, 102, 153 }, { 204, 102, 102 }, { 204, 102, 51 },
    { 204, 102, 0 }, { 153, 102, 255 }, { 153, 102, 204 }, { 153, 102, 153 }, { 153, 102, 102 }, { 153, 102, 51 }, { 153, 102, 0 }, { 128, 128, 255 },
    { 102, 102, 255 }, { 128, 128, 192 }, { 102, 102, 204 }, { 102, 102, 153 }, { 102, 102, 102 }, { 102, 102, 51 }, { 102, 102, 0 }, { 0, 128, 255 },
    { 51, 102, 255 }, { 51, 102, 204 }, { 51, 102, 153 }, { 51, 102, 102 }, { 51, 102, 51 }, { 51, 102, 0 }, { 0, 102, 255 }, { 0, 102, 204 },
    { 0, 102, 153 }, { 0, 102, 102 }, { 0, 102, 51 }, { 0, 102, 0 }, { 255, 51, 255 }, { 255, 51, 204 }, { 255, 51, 153 }, { 255, 51, 102 },
    { 255, 51, 51 }, { 255, 51, 0 }, { 204, 51, 255 }, { 204, 51, 204 }, { 204, 51, 153 }, { 204, 51, 102 }, { 204, 51, 51 }, { 204, 51, 0 },
    { 153, 51, 255 }, { 153, 51, 204 }, { 153, 51, 153 }, { 153, 51, 102 }, { 153, 51, 51 }, { 153, 51, 0 }, { 102, 51, 255 }, { 102, 51, 204 },
    { 102, 51, 153 }, { 102, 51, 102 }, { 128, 64, 64 }, { 102, 51, 51 }, { 102, 51, 0 }, { 51, 51, 255 }, { 51, 51, 204 }, { 51, 51, 153 },
    { 51, 51, 102 }, { 64, 64, 64 }, { 51, 51, 51 }, { 64, 64, 0 }, { 51, 51, 0 }, { 0, 51, 255 }, { 0, 51, 204 }, { 0, 51, 153 },
    { 0, 64, 128 }, { 0, 51, 102 }, { 0, 64, 64 }, { 0, 51, 51 }, { 0, 64, 0 }, { 0, 51, 0 }, { 255, 0, 204 }, { 255, 0, 153 },
    { 255, 0, 102 }, { 255, 0, 51 }, { 204, 0, 255 }, { 204, 0, 204 }, { 192, 0, 192 }, { 204, 0, 153 }, { 204, 0, 102 }, { 204, 0, 51 },
    { 204, 0, 0 }, { 192, 0, 0 }, { 170, 0, 0 }, { 153, 0, 255 }, { 153, 0, 204 }, { 153, 0, 153 }, { 153, 0, 102 }, { 153, 0, 51 },
    { 153, 0, 0 }, { 102, 0, 255 }, { 128, 0, 255 }, { 102, 0, 204 }, { 102, 0, 153 }, { 102, 0, 102 }, { 128, 0, 64 }, { 102, 0, 51 },
    { 102, 0, 0 }, { 64, 0, 0 }, { 51, 0, 255 }, { 51, 0, 204 }, { 51, 0, 153 }, { 64, 0, 128 }, { 51, 0, 102 }, { 64, 0, 64 },
    { 51, 0, 51 }, { 51, 0, 0 }, { 0, 0, 204 }, { 0, 0, 170 }, { 0, 0, 153 }, { 0, 0, 102 }, { 0, 0, 64 }, { 0, 0, 51 } };

static void op_color(int op, Uint8 *r, Uint8 *g, Uint8 *b) {
    static const uint8_t C[8][3] = { { 0, 200, 255 }, { 80, 255, 120 }, { 255, 160, 40 }, { 255, 80, 200 },
                                     { 160, 120, 255 }, { 255, 255, 90 }, { 90, 255, 230 }, { 255, 110, 110 } };
    int k = 0;
    for (int i = 0; i < nops; i++) if (ops[i] == op) { k = i; break; }
    *r = C[k % 8][0]; *g = C[k % 8][1]; *b = C[k % 8][2];
}

static SDL_FPoint *scr; static int capscr;

static void draw_poly(SDL_Renderer *rd, const Poly *p, int limit) {
    int n = p->count < limit ? p->count : limit;
    if (n < 2) return;
    if (n > capscr) { capscr = n * 2; scr = realloc(scr, capscr * sizeof(SDL_FPoint)); }
    for (int i = 0; i < n; i++) project(pts[p->first + i], &scr[i].x, &scr[i].y);
    SDL_RenderDrawLinesF(rd, scr, n);
}

static void draw_circle(SDL_Renderer *rd, V3 c, double r) {
    SDL_FPoint q[49];
    for (int i = 0; i <= 48; i++) {
        V3 p = { c.x + r * cos(i * M_PI / 24), c.y + r * sin(i * M_PI / 24), c.z };
        project(p, &q[i].x, &q[i].y);
    }
    SDL_RenderDrawLinesF(rd, q, 49);
}

static void render(SDL_Renderer *rd) {
    SDL_SetRenderDrawColor(rd, 18, 22, 30, 255);
    SDL_RenderClear(rd);
    /* axes at the origin */
    V3 o = { 0, 0, 0 }, ax[3] = { { 10, 0, 0 }, { 0, 10, 0 }, { 0, 0, 10 } };
    Uint8 axc[3][3] = { { 220, 60, 60 }, { 60, 220, 60 }, { 70, 110, 255 } };
    for (int i = 0; i < 3; i++) {
        float x0, y0, x1, y1; project(o, &x0, &y0); project(ax[i], &x1, &y1);
        SDL_SetRenderDrawColor(rd, axc[i][0], axc[i][1], axc[i][2], 255);
        SDL_RenderDrawLineF(rd, x0, y0, x1, y1);
    }
    int selop = sel ? ops[sel - 1] : 0;
    long budget = sim_n < 0 ? (long)npts * 4 : sim_n;    /* simulation: number of toolpath points drawn */
    V3 toolpos = { 0, 0, 0 }; int have_tool = 0, curtool = 0;
    for (int i = 0; i < npolys; i++) {
        const Poly *p = &polys[i];
        if (p->kind == K_GEOM) {
            if (!show_geom) continue;
            const uint8_t *c = PAL[p->color];
            SDL_SetRenderDrawColor(rd, c[0], c[1], c[2], 255);
            draw_poly(rd, p, p->count);
            continue;
        }
        if (!show_tp || (selop && p->op != selop)) continue;
        if (budget <= 0) continue;
        int lim = p->count < budget ? p->count : (int)budget;
        budget -= p->count;
        if (p->kind == K_RAPID) {
            if (!show_rapid) continue;
            SDL_SetRenderDrawColor(rd, 200, 200, 60, 255);
        } else {
            Uint8 r, g, b; op_color(p->op, &r, &g, &b);
            SDL_SetRenderDrawColor(rd, r, g, b, 255);
        }
        draw_poly(rd, p, lim);
        if (lim > 0) { toolpos = pts[p->first + lim - 1]; have_tool = 1; curtool = p->tool; }
    }
    if (sim_n >= 0 && have_tool) {                        /* tool marker during simulation */
        double dia = tool_dia(curtool);
        SDL_SetRenderDrawColor(rd, 255, 255, 255, 255);
        draw_circle(rd, toolpos, dia > 0 ? dia / 2 : 1);
        V3 top = toolpos; top.z += 10;
        float x0, y0, x1, y1; project(toolpos, &x0, &y0); project(top, &x1, &y1);
        SDL_RenderDrawLineF(rd, x0, y0, x1, y1);
    }
}

static void set_title(SDL_Window *win, const char *path) {
    char t[512];
    const char *base = path_sep_r(path); base = base ? base + 1 : path;
    if (sel) snprintf(t, sizeof t, "%s - op %d (%d/%d)%s", base, ops[sel - 1], sel, nops, playing ? " [sim]" : "");
    else snprintf(t, sizeof t, "%s - all %d ops, %ld lines, %ld arcs, %ld moves%s", base, nops, nlines, narcs, nmoves,
                  playing ? " [sim]" : "");
    SDL_SetWindowTitle(win, t);
}

/* ------------------------------------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s PART.mcx-9 [--shot out.bmp] [--iso] [--op N] [--size WxH]\n", argv[0]);
        return 1;
    }
    const char *shot = NULL;
    int iso = 0, want_op = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--iso")) iso = 1;
        else if (!strcmp(argv[i], "--op") && i + 1 < argc) want_op = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
    }
    if (!load(argv[1])) return 1;
    printf("%s: %ld lines, %ld arcs (%ld off-plane), %ld points, %d tools, %d ops, %ld NCI moves (%ld arcs, %ld drill)\n",
           argv[1], nlines, narcs, arc_fixups, npoints_ent, ntools, nops, nmoves, nnciarcs, ndrill);
    for (int i = 0; i < nops; i++) if (ops[i] == want_op) sel = i + 1;
    if (iso) { yaw = -M_PI / 4; pitch = 1.0; }
    fit();

    if (shot) {                                           /* offscreen render to BMP */
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer *rd = SDL_CreateSoftwareRenderer(s);
        render(rd);
        SDL_RenderPresent(rd);
        if (SDL_SaveBMP(s, shot) != 0) { fprintf(stderr, "save: %s\n", SDL_GetError()); return 1; }
        printf("wrote %s\n", shot);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("mcamview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *rd = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rd) rd = SDL_CreateRenderer(win, -1, 0);
    set_title(win, argv[1]);
    int running = 1, drag = 0;
    Uint32 last = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) SDL_GetRendererOutputSize(rd, &W, &H);
                break;
            case SDL_MOUSEBUTTONDOWN: drag = e.button.button; break;
            case SDL_MOUSEBUTTONUP: drag = 0; break;
            case SDL_MOUSEMOTION:
                if (drag == SDL_BUTTON_LEFT) {
                    yaw += e.motion.xrel * 0.01; pitch += e.motion.yrel * 0.01;
                    if (pitch < -M_PI / 2) pitch = -M_PI / 2; if (pitch > M_PI / 2) pitch = M_PI / 2;
                } else if (drag) { panx += e.motion.xrel; pany += e.motion.yrel; }
                break;
            case SDL_MOUSEWHEEL: {
                double f = e.wheel.y > 0 ? 1.15 : 1 / 1.15;
                int mx, my; SDL_GetMouseState(&mx, &my);
                /* zoom about the mouse position */
                panx = (panx + W * 0.5 - mx) * f - (W * 0.5 - mx);
                pany = (pany + H * 0.5 - my) * f - (H * 0.5 - my);
                scale *= f;
                break;
            }
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: case SDLK_q: running = 0; break;
                case SDLK_t: yaw = 0; pitch = 0; break;
                case SDLK_i: yaw = -M_PI / 4; pitch = 1.0; break;
                case SDLK_f: yaw = 0; pitch = M_PI / 2; break;
                case SDLK_z: fit(); break;
                case SDLK_g: show_geom ^= 1; break;
                case SDLK_p: show_tp ^= 1; break;
                case SDLK_r: show_rapid ^= 1; break;
                case SDLK_RIGHTBRACKET: sel = (sel + 1) % (nops + 1); sim_n = playing ? 0 : -1; break;
                case SDLK_LEFTBRACKET: sel = (sel + nops) % (nops + 1); sim_n = playing ? 0 : -1; break;
                case SDLK_SPACE:
                    playing ^= 1;
                    if (playing && sim_n < 0) sim_n = 0;
                    break;
                case SDLK_BACKSPACE: sim_n = 0; break;
                case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: sim_speed *= 2; break;
                case SDLK_MINUS: case SDLK_KP_MINUS: sim_speed = fmax(1, sim_speed / 2); break;
                case SDLK_ESCAPE + 1000: break;
                }
                set_title(win, argv[1]);
                break;
            }
        }
        Uint32 now = SDL_GetTicks();
        if (playing && sim_n >= 0) {
            sim_n += (long)(sim_speed * (now - last) / 1000.0) + 1;
            if (sim_n > npts) { sim_n = -1; playing = 0; set_title(win, argv[1]); }
        }
        last = now;
        render(rd);
        SDL_RenderPresent(rd);
    }
    SDL_Quit();
    return 0;
}
