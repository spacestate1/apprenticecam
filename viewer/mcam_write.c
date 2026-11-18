/* mcam_write - see mcam_write.h. Framing follows mcam_load.c (and ../formats/mcam.md):
 *   [u32 len + ent]...   (system ent 0xF is followed by u32 count + count * 0x1ae0 op_group,
 *                         system ent 0xE by material records until id 0, solid ents by XT bytes + trailer)
 *   [u16 size + nci_bin]...   until u16 0 or EOF                                                   */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "mcam_write.h"
#include "mcam_grp.h"

static uint32_t rd32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t rd16(const uint8_t *b) { return b[0] | b[1] << 8; }
static uint64_t rd64(const uint8_t *b) { return rd32(b) | (uint64_t)rd32(b + 4) << 32; }

static void add(McwFile *m, int kind, size_t off, size_t len, uint32_t cls, int32_t sub, int32_t id) {
    m->seg = xrealloc(m->seg, sizeof(McwSeg) * (m->nseg + 1));
    m->seg[m->nseg++] = (McwSeg){ kind, off, len, cls, sub, id };
}

int mcw_open(McwFile *m, const char *path, char *err, size_t errn) {
    FILE *f = fopen(path, "rb");
    if (!f) { memset(m, 0, sizeof *m); snprintf(err, errn, "cannot open %s: %s", path, strerror(errno)); return 0; }
    long L = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1; if (L >= 0 && fseek(f, 0, SEEK_SET) != 0) L = -1;
    if (L < 0x9c || L > (1L << 30)) { fclose(f); memset(m, 0, sizeof *m); snprintf(err, errn, "%s: too small", path); return 0; }
    uint8_t *d = xmalloc((size_t)L);
    if (fread(d, 1, (size_t)L, f) != (size_t)L) { fclose(f); free(d); memset(m, 0, sizeof *m); snprintf(err, errn, "%s: read error", path); return 0; }
    fclose(f);
    int ok = mcw_open_mem(m, d, (size_t)L, err, errn);
    snprintf(m->path, sizeof m->path, "%s", path);
    return ok;
}

int mcw_open_mem(McwFile *m, uint8_t *data, size_t n, char *err, size_t errn) {
    memset(m, 0, sizeof *m);
    m->first_assoc = m->first_solid = m->first_nci = -1;
    m->d = data; m->n = n;
    const uint8_t *d = m->d; size_t len = m->n, off = 0;
    if (len < 0x9c || rd32(d) != 0x98 || rd32(d + 4) != 0x200 || rd16(d + 4 + 0x68) != 8) { snprintf(err, errn, "not an X9 / 2017 part file (.mcx-9 / .mcam)"); return 0; }
    if (mcam_part_newer(d, len)) { snprintf(err, errn, "%s", MCAM_NEWER_MSG); return 0; }   /* a newer release's part (mcam_grp.h, DoFileOpen) */
    m->major = d[4 + 0x6a]; m->minor = d[4 + 0x6b];
    while (off + 4 <= len) {
        uint32_t n = rd32(d + off);
        if (n == 0 || off + 4 + n > len) break;                      /* end of entities: the NCI tail follows */
        const uint8_t *rec = d + off + 4;
        uint32_t cls = n >= 4 ? rd32(rec) : 0;
        int32_t sub = cls == 0x200 && n >= 0x6a ? rd16(rec + 0x68) : cls == 0x4000 && n >= 0x30 ? (int32_t)rd32(rec + 0x2c) : -1;
        int32_t id = n >= 0x38 ? (int32_t)rd32(rec + 0x34) : 0;
        if (n >= 0x38 && id > m->max_ent_idn) m->max_ent_idn = id;
        if (cls == 0x4000 && sub == 1 && n >= 0x6c) { int32_t op = (int32_t)rd32(rec + 0x68); if (op > m->max_op_id) m->max_op_id = op; }
        if (cls == 0x4000 && m->first_assoc < 0) m->first_assoc = m->nseg;
        if (cls == 0x20000 && m->first_solid < 0) m->first_solid = m->nseg;
        add(m, SEG_ENT, off, 4 + n, cls, sub, id);
        off += 4 + n;
        if (cls == 0x200 && n >= 0x6a && sub == 0xF && off + 4 <= len) {               /* toolpath groups */
            size_t o0 = off, sz = mcam_grp_block(d + off, len - off, NULL, 0, NULL);   /* the records and their member entries (mcam_grp.h) */
            if (!sz) { snprintf(err, errn, "bad group block"); return 0; }
            off += sz; add(m, SEG_OOB, o0, off - o0, 0, 0xF, 0);
        } else if (cls == 0x200 && n >= 0x6a && sub == 0xE) {                           /* material table */
            size_t o0 = off;
            while (off + 0x28 <= len) {
                int32_t mid = (int32_t)rd32(d + off); uint64_t s = rd64(d + off + 0x10);
                off += 0x28;
                if (mid == 0) break;
                if (s > len - off) { snprintf(err, errn, "bad material block"); return 0; }
                off += s;
            }
            add(m, SEG_OOB, o0, off - o0, 0, 0xE, 0);
        } else if (cls == 0x20000 && n >= 0x16c) {                                       /* solid: XT + trailer */
            size_t o0 = off; uint8_t ver = rec[0x49]; uint32_t xs = rd32(rec + 0x168);
            if (xs > len - off) { snprintf(err, errn, "bad solid"); return 0; }
            off += xs + 4 + (ver > 0x1b ? 0x100 : 0x40) + 1 + 1 + 4 + 4 + 8;
            if (off + 4 > len) { snprintf(err, errn, "bad solid trailer"); return 0; }
            uint32_t cnt = rd32(d + off); off += 4;
            for (uint32_t i = 0; i < cnt; i++) { if (off + 8 > len) { snprintf(err, errn, "bad solid history"); return 0; } off += 8 + rd32(d + off + 4); }
            if (off > len) { snprintf(err, errn, "bad solid history"); return 0; }
            add(m, SEG_SOLID_TAIL, o0, off - o0, 0x20000, 0, 0);
        }
    }
    m->first_nci = m->nseg;
    while (off + 2 <= len) {                                          /* binary NCI records */
        uint16_t sz = rd16(d + off);
        if (sz == 0 || off + 2 + sz > len) break;
        int16_t g = (int16_t)rd16(d + off + 2);
        add(m, SEG_NCI, off, 2 + sz, 0, g, 0);
        off += 2 + sz;
    }
    if (off < len) add(m, SEG_NCI, off, len - off, 0, 0x7fff, 0);       /* whatever ends the file (u16 0 marker, padding) */
    if (m->first_nci == m->nseg) m->first_nci = -1;
    return 1;
}

void mcw_close(McwFile *m) { free(m->d); free(m->seg); memset(m, 0, sizeof *m); }

/* the file built in memory: source segments (those with keep[i] != 0, or all when keep is NULL) + insertions */
uint8_t *mcw_build(const McwFile *m, const char *keep, const McwInsert *ins, int nins, size_t *outn) {
    size_t n = 0;
    for (int i = 0; i < m->nseg; i++) if (!keep || keep[i]) n += m->seg[i].len;
    for (int k = 0; k < nins; k++) n += ins[k].len;
    uint8_t *o = xmalloc(n ? n : 1), *q = o;
    for (int i = 0; i <= m->nseg; i++) {
        for (int k = 0; k < nins; k++) if (ins[k].before == i && ins[k].len) { memcpy(q, ins[k].bytes, ins[k].len); q += ins[k].len; }
        if (i < m->nseg && (!keep || keep[i])) { memcpy(q, m->d + m->seg[i].off, m->seg[i].len); q += m->seg[i].len; }
    }
    *outn = n;
    return o;
}

int mcw_write(const McwFile *m, const McwInsert *ins, int nins, const char *path, char *err, size_t errn) {
    char tmp[4200]; if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) { snprintf(err, errn, "path too long"); return 0; }
    FILE *o = fopen(tmp, "wb");
    if (!o) { snprintf(err, errn, "cannot write %s: %s", tmp, strerror(errno)); return 0; }
    for (int i = 0; i <= m->nseg; i++) {
        for (int k = 0; k < nins; k++) if (ins[k].before == i) fwrite(ins[k].bytes, 1, ins[k].len, o);
        if (i < m->nseg) fwrite(m->d + m->seg[i].off, 1, m->seg[i].len, o);
    }
    int ok = !ferror(o);
    if (fclose(o) != 0) ok = 0;
    if (!ok || file_replace(tmp, path) != 0) { remove(tmp); snprintf(err, errn, "writing %s failed: %s", path, strerror(errno)); return 0; }
    return 1;
}
