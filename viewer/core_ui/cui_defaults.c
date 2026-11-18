/* cui_defaults.c - Q19 "Save parameters to defaults file" (op dialog toolbar 9433) and the ours commands that keep the defaults
   file easy to reset (user decision, formats/user_decisions.md "Q19 addition"). formats/opdlg_port_audit.md 22.
   Decompiles: decomp/b12/tpdlg_all.c (ToolpathDlg::OnWriteToDefaultsPb 0x180894ed0, WriteOperationToDefaultsFile 0x180896fc0),
   ~/mastercam-re/work/defs_decode/ (copied to decomp/defs/): core_oplib.c (oplib_rewrite / oplib_save / oplib_delete / oplib_list,
   lib_rewrite 0x18006dd70, lib_delete, lib_save, TpOpList::SortOperations 0x180903d80, InitLocalMachine, AddDefaultsButtons),
   core_oplib2.c (GetMastercamFileVersionM, write_ent_to_disk, lib_read_ent), core_oplib3.c (FUN_18006c9a0 = lib_entity_size,
   FUN_180067b50 = the id match), mc_grpdefaults.c (mastercam.dll OMgetGrpDefaultsFile, OpDfltsFile).

   Mastercam (decoded):
   - OnWriteToDefaultsPb: this[0x729c] == 0 (the single-op dialog) -> OnUpdateLocalOp (every page's +0x3b8: the pages into the
     dialog's op) -> WriteOperationToDefaultsFile(dialog op +0x20f0) (vtable +0x590) -> true: InitLocalMachine (+0x398: the op's
     machine group / machine pointers again, nothing about the file).
   - WriteOperationToDefaultsFile: the file = OMgetGrpDefaultsFile(0) (the active machine group's OpDfltsFile; none -> false, no
     message). When it exists: GetMastercamFileVersion (the CString overload: major = version_header +2, the "64 bit" byte = +5);
     major > the running version (DAT_18143d54a) -> MCMessageBox(mccoreres 5524 + '\n' + 5525), false; an older major >= 10 other
     than SaveAsXVersion -> MCMessageBox(5518 + '\n' + 5519(%1 = this version, %2 = the old one) + '\n' + 5516, OK / Cancel), OK ->
     ConvertFile, Cancel -> false; the same major with the 64-bit byte 0 -> MCMessageBox(5520 + "\n\n" (DAT_180cf5448) + 5525),
     false. Then oplib_list(file) (TpOpList::SortOperations(7, 1, 1): the ops whose op +0x95 is set are put in the order of op
     +0x7e among their own places) and the first op with the dialog op's opcode (+4) - for 0x84 also the same byte +0x1654, for
     0x86 the same byte +0x11c0 - gives: its id (+0), +0x28c (group), the block +0x80 .. +0xf7 (NCI offsets, TP count, chain counts,
     dirty +0x94, ...), its op struct version (+0x11b0) and for 0x84 / opcodes 9, 10, 0xf, 0x2e, 0x83, 0x6b the short +0x15e6 /
     +0x1282. The dialog op is copied, id / +0x28c / +0x80..+0xf7 replaced by the file op's; same short and same version ->
     oplib_rewrite(id, file, op) = lib_rewrite: the op record found by id (class 0x4000, ASSOC 1, selection bit 1) is read, its
     0x28d0-byte body (ent +0x68, lib_entity_size(1)) replaced, and written back at the same place (write_ent_to_disk ->
     FUN_1802c72c0, which stamps ent +0x48 / +0x49 / +0x4b..+0x4e), then message_box_ok(mccoreres 5419 "Library updated
     successfully."); else oplib_delete + oplib_save (append); no op of the type / no file -> init_operation_db + oplib_save.
     There is no confirmation before the write.
   Here (ported): the rewrite branch, byte for byte as above, into the program's own copy of the file (never Mastercam's: paths.h
   defaults_*). The dialog op = the record this program's .mcam save writes for the op (mcam_saveops.c mcam_op_dialog_record).
   Also ported (S-D1, 2026-10-24): the delete + append branch (dfl_delete_append) and the X9 -> 2017 op record update of a stored
   op of a .mcx-9 part (mcam_x9_op_update; its op version stays 19.1, so its save takes the delete + append branch).
   NOT DECODED / refused with a message (nothing written): ConvertFile (an older file), init_operation_db + oplib_save (no op of the
   type: init_operation_db's redo_op_idn is a run-time counter) and a new file, lib_delete's reference counts (header +0x6c >= 1 or
   ASSOC 2 records of the op), GetUpdatedOpcodeMW (X9 opcode 0x1b8), SaveAsXVersion's value.
   Ours (user decision Q19 addition): before the first write the original is kept as a pristine copy, every save first keeps a
   dated backup of the working copy, "Reset defaults file to original (ours)" and "Restore a dated backup (ours)" with a
   confirmation; the record stamp = the file's header record's (as mcam_saveops.c); the readers' caches are dropped after a write. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "platform.h"
#include "paths.h"
#include "mcam_write.h"
#include "mcam_saveops.h"
#include "cam_fileop.h"
#include "core_ui.h"

static uint32_t rd32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t rd16(const uint8_t *b) { return b[0] | b[1] << 8; }
static double rdd(const uint8_t *b) { double v; memcpy(&v, b, 8); return v; }

#define OP_ENT 0x2938                                 /* ent 0x68 + operation 0x28d0 (lib_entity_size(1)) */
#define CUR_MAJOR 0x13                                /* Mastercam 2017 (DAT_18143d54a: the running version; .mcam major) */

/* ---------------------------------------------------------------------------------------------- the program's copies (ours) */
typedef struct { char real[1100], work[1700], pristine[1700], bdir[1700], name[300]; } Dfl;
static int dfl_paths(Dfl *d, const char *real) {
    memset(d, 0, sizeof *d);
    char dir[1100], rel[600];
    if (!real || !real[0] || !defaults_key(real, rel, sizeof rel)) return 0;
    snprintf(d->real, sizeof d->real, "%s", real);
    defaults_dir(dir, sizeof dir);
    char t[1700];
    path_join(d->work, sizeof d->work, dir, rel);
    snprintf(t, sizeof t, "pristine/%s", rel); path_join(d->pristine, sizeof d->pristine, dir, t);
    snprintf(t, sizeof t, "backups/%s", rel); { char *s = strrchr(t, '/'); if (s) *s = 0; } path_join(d->bdir, sizeof d->bdir, dir, t);
    { const char *b = strrchr(rel, '/'); snprintf(d->name, sizeof d->name, "%s", b ? b + 1 : rel); }
    return 1;
}
static int is_file(const char *p) { struct stat st; return p[0] && stat(p, &st) == 0 && S_ISREG(st.st_mode); }
/* a write target must lie in the program's defaults folder (never Mastercam's files) */
static int in_defaults_dir(const char *p) {
    char dir[1100]; defaults_dir(dir, sizeof dir); size_t L = strlen(dir);
    while (L && is_sep(dir[L - 1])) L--;
    return L && !strncmp(p, dir, L) && is_sep(p[L]);
}
static uint8_t *read_all(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    long L = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (L < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t *b = xmalloc((size_t)L + 1);
    if (fread(b, 1, (size_t)L, f) != (size_t)L) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)L; return b;
}
static void parent_dirs(const char *p) {
    char d[1700]; snprintf(d, sizeof d, "%s", p);
    char *s = path_sep_r(d); if (s) { *s = 0; make_dirs(d); }
}
/* DST = BYTES (a temporary file next to it, then renamed: a failed write leaves DST as it was) */
static int write_all(const char *dst, const uint8_t *b, size_t n, char *err, size_t en) {
    if (!in_defaults_dir(dst)) { snprintf(err, en, "refused: %s is outside the program's defaults folder", dst); return 0; }
    parent_dirs(dst);
    char tmp[1800]; snprintf(tmp, sizeof tmp, "%s.tmp", dst);
    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(err, en, "cannot write %s: %s", tmp, strerror(errno)); return 0; }
    int ok = fwrite(b, 1, n, f) == n; ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp); snprintf(err, en, "cannot write %s", tmp); return 0; }
#ifdef _WIN32
    remove(dst);
#endif
    if (rename(tmp, dst) != 0) { remove(tmp); snprintf(err, en, "cannot replace %s: %s", dst, strerror(errno)); return 0; }
    return 1;
}
static int copy_file(const char *src, const char *dst, char *err, size_t en) {
    size_t n; uint8_t *b = read_all(src, &n);
    if (!b) { snprintf(err, en, "cannot read %s", src); return 0; }
    int ok = write_all(dst, b, n, err, en); free(b); return ok;
}
/* the dated backup name: <file>.<YYYY-MM-DD_HHMMSS>[_k] (local time; _k when that second is taken) */
static int backup_now(const Dfl *d, char *name, size_t nn, char *err, size_t en) {
    time_t t = time(NULL); struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[40]; strftime(stamp, sizeof stamp, "%Y-%m-%d_%H%M%S", &tm);
    char p[1800];
    for (int k = 0; k < 1000; k++) {
        if (k) snprintf(name, nn, "%s.%s_%d", d->name, stamp, k); else snprintf(name, nn, "%s.%s", d->name, stamp);
        path_join(p, sizeof p, d->bdir, name);
        if (!is_file(p)) return copy_file(d->work, p, err, en);
    }
    snprintf(err, en, "no free backup name in %s", d->bdir); return 0;
}
/* before a write of the working copy: the pristine copy (the original, once), the working copy (from the original, once), a
   dated backup of the working copy as it is now. BNAME = the backup's name. */
static int start_write(const Dfl *d, char *bname, size_t bn, char *err, size_t en) {
    if (!is_file(d->pristine)) { if (!is_file(d->real) || !copy_file(d->real, d->pristine, err, en)) { if (!err[0]) snprintf(err, en, "cannot read %s", d->real); return 0; } }
    if (!is_file(d->work) && !copy_file(d->pristine, d->work, err, en)) return 0;
    return backup_now(d, bname, bn, err, en);
}
static int name_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
/* the dated backups of D, oldest first (the names sort by date) */
static int list_backups(const Dfl *d, char (*names)[160], int max) {
    DIR *dir = opendir(d->bdir); if (!dir) return 0;
    struct dirent *de; int n = 0; size_t L = strlen(d->name);
    while ((de = readdir(dir)) && n < max)
        if (!strncmp(de->d_name, d->name, L) && de->d_name[L] == '.' && strlen(de->d_name) < 160 && !strstr(de->d_name, ".tmp"))
            snprintf(names[n++], 160, "%s", de->d_name);
    closedir(dir);
    qsort(names, (size_t)n, 160, name_cmp);
    return n;
}

/* the readers' cached copies of the defaults files (cui_ops.c default_op, cam_fileop.c, mcam_saveops.c, cui_toolpage.c) */
static void readers_reset(void) { default_op_reset(); cam_defaults_reset(); mcam_defaults_library_reset(); tl6410_mach_reset(); }

/* ---------------------------------------------------------------------------------------------- WriteOperationToDefaultsFile */
/* TpOpList::SortOperations(7, 1, 1) on oplib_list's ops (file order: lib_list reads the file's live op records): the ops with op
   +0x95 (record 0xfd) set take their own places in the order of op +0x7e (record 0xe6), the selection sort keeping the first of
   equals; ops with an id < 1 are left where they are. Returns the record segment indices in list order. */
static int op_list(const McwFile *m, int *out, int max) {
    int n = 0;
    for (int i = 0; i < m->nseg && n < max; i++) {
        const McwSeg *s = &m->seg[i]; const uint8_t *e = m->d + s->off + 4;
        if (s->kind != SEG_ENT || s->cls != 0x4000 || s->sub != 1 || s->len < 4 + 0x6e) continue;
        if (!(rd16(e + 4) & 1)) continue;                            /* sel_bit_is_on(ent, 1): a deleted op is skipped (lib_delete) */
        out[n++] = i;
    }
    int idx[4096], k = 0, srt[4096];
    for (int j = 0; j < n && k < 4096; j++) { const uint8_t *e = m->d + m->seg[out[j]].off + 4; if (e[0xfd] && (int32_t)rd32(e + 0x68) >= 1) idx[k++] = j; }
    for (int a = 0; a < k; a++) srt[a] = out[idx[a]];
    for (int a = 0; a < k; a++) {                                   /* selection sort by op +0x7e, first of equals */
        int best = a;
        for (int b = a + 1; b < k; b++) if ((int16_t)rd16(m->d + m->seg[srt[b]].off + 4 + 0xe6) < (int16_t)rd16(m->d + m->seg[srt[best]].off + 4 + 0xe6)) best = b;
        int t = srt[a]; for (int b = best; b > a; b--) srt[b] = srt[b - 1]; srt[a] = t;
    }
    for (int a = 0; a < k; a++) out[idx[a]] = srt[a];
    return n;
}
/* the first op of the list matching the dialog op (opcode +4; 0x84: byte +0x1654, 0x86: byte +0x11c0); -1 = none */
static int match_op(const McwFile *m, const uint8_t *dop) {
    int l[8192], n = op_list(m, l, 8192);
    int16_t oc = (int16_t)rd16(dop + 0x6c);
    for (int j = 0; j < n; j++) {
        const McwSeg *s = &m->seg[l[j]]; const uint8_t *e = m->d + s->off + 4;
        if (s->len != 4 + OP_ENT) continue;                          /* the op records this program writes (no variable tail) */
        int16_t so = (int16_t)rd16(e + 0x6c);
        if (so != oc) continue;
        if (oc == 0x84 && e[0x68 + 0x1654] != dop[0x68 + 0x1654]) continue;
        if (oc == 0x86 && e[0x68 + 0x11c0] != dop[0x68 + 0x11c0]) continue;
        return l[j];
    }
    return -1;
}
/* the version checks (decoded above); 0 = refused, MSG = Mastercam's message */
/* mccoreres strings (~/mastercam-re/regress/str/all.tsv) */
#define S5524 "The file selected for export was last saved in a newer version of Mastercam."
#define S5525 "This file cannot be updated by this version of Mastercam."
#define S5518 "The file you are about to overwrite is from a previous version of Mastercam."
#define S5520 "The file you are about to overwrite was last saved in the 32 bit version of Mastercam."
#define S5419 "Library updated successfully."
static int version_ok(const McwFile *m, char *msg, size_t n) {
    if (m->major > CUR_MAJOR) { snprintf(msg, n, "%s\n%s", S5524, S5525); return 0; }
    if (m->major < CUR_MAJOR && m->major >= 10) {
        /* 5518 + '\n' + 5519 + '\n' + 5516 with OK / Cancel, OK -> ConvertFile: NOT DECODED here, nothing written (ours) */
        snprintf(msg, n, "%s\n\n(apprenticeCAM: converting the file to this version is not decoded - nothing was written.)", S5518); return 0; }
    if (!m->d[4 + 0x68 + 5]) { snprintf(msg, n, "%s\n\n%s", S5520, S5525); return 0; }   /* the CString overload's byte (version_header +5) */
    return 1;
}

static void wr16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *b, uint32_t v) { for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> 8 * i); }
static uint64_t rd64(const uint8_t *b) { return rd32(b) | (uint64_t)rd32(b + 4) << 32; }
static void wr64(uint8_t *b, uint64_t v) { wr32(b, (uint32_t)v); wr32(b + 4, (uint32_t)(v >> 32)); }

#define S4984 "Exporting entities to files that contain NCI is not allowed."
/* The other branch of WriteOperationToDefaultsFile (the op version or the short +0x15e6 / +0x1282 differs from the file op's):
   oplib_delete(file op id) + oplib_save(op, file, message 1, add to list 0). Decompiles decomp/sd1/core_prevver.c (lib_delete
   0x18006bfa0, oplib_save 0x1800ca790, lib_save 0x18006e4a0, FUN_1800697e0 = the record write, FUN_18006a550, FUN_180069120),
   decomp/sd1/core_libid.c (lib_unique_id 0x18006e880, lib_nci_fpos, tp_no_ref, ent_is_assoc, sel_bit_*), decomp/defs/core_oplib2.c
   (lib_read_hdr: the first record class 0x200 sub 0xd), core_oplib3.c (FUN_18006ec20: lib_compress only for an older major).
   lib_delete (ASSOC 1): the first live op record (selection bit 1, ent +4) with the id (FUN_180067b50) before the NCI position gets
   bit 1 off (the 6 bytes at its ent start written again), the header's op count (i16 +0x70) - 1; then every live ASSOC 2 record
   (tp_entity) of the op (+0x68 = op id) loses bit 1 too, and when the header's i32 +0x6c >= 1 the referenced entity's count
   (+0x38) is decremented (NOT PORTED: refused below; the shipped Mill_MM / Mill_Inch files have +0x6c = 0 and no ASSOC 2 record);
   the header is written back with +0x70 / +0x6c clamped to >= 0. lib_save: a new record (class 0x4000, ASSOC 1, selection bits
   1 | 0x100, the op body); FUN_1800697e0: the header read again; its NCI position (i32 +0x78) > 0 -> post_warning(mccoreres 4984),
   nothing written; lib_unique_id over the live records before the NCI position (stopping at the header when its +0x78 >= 0): the
   op id = 1 + the highest live ASSOC 1 id (+0x68), the record's +0x20 = 0x30 + the highest live +0x20; the header's +0x7c + 1 = the
   record's +0x34; the record appended at the end of the file (write_ent_to_disk: u32 0x2938 + the record, FUN_1802c72c0's stamp:
   ours from the header record, as the rewrite); the header's +0x74 = the record's position when it was < 0; the header written
   back; message_box_ok(5419). REC = the dialog op's record with the file op's id / +0x80..+0xf7 / +0x28c (as the rewrite branch).
   On success *W / *WN are the new file, *OFF the new record's ent offset; 0 + MSG = refused (nothing changed) */
static int dfl_delete_append(const McwFile *m, int si, const uint8_t *rec, uint8_t **W, size_t *WN, size_t *OFF, char *msg, size_t mn) {
    uint8_t *w = *W; size_t wn = *WN;
    int hi = -1;
    for (int i = 0; i < m->nseg && hi < 0; i++) if (m->seg[i].kind == SEG_ENT && m->seg[i].cls == 0x200 && m->seg[i].sub == 0x0d && m->seg[i].len >= 4 + 0x80) hi = i;
    if (hi < 0) { snprintf(msg, mn, "the defaults file has no header record"); return 0; }
    uint8_t *h = w + m->seg[hi].off + 4;
    int32_t opid = (int32_t)rd32(rec + 0x68);
    if ((int32_t)rd32(h + 0x6c) >= 1) { snprintf(msg, mn, "the defaults file counts referenced entities (header +0x6c = %d); updating their reference counts (lib_delete) is not ported", (int)(int32_t)rd32(h + 0x6c)); return 0; }
    for (int i = 0; i < m->nseg; i++) {
        const McwSeg *s = &m->seg[i]; const uint8_t *e = w + s->off + 4;
        if (s->kind == SEG_ENT && s->cls == 0x4000 && s->sub == 2 && s->len >= 4 + 0x6c && (rd16(e + 4) & 1) && (int32_t)rd32(e + 0x68) == opid) {
            snprintf(msg, mn, "the defaults file holds geometry references of the operation (ASSOC 2); deleting them (lib_delete) is not ported"); return 0; }
    }
    /* lib_delete */
    uint8_t *oe = w + m->seg[si].off + 4;
    wr16(oe + 4, (uint16_t)(rd16(oe + 4) & ~1u));
    wr16(h + 0x70, (uint16_t)((int16_t)rd16(h + 0x70) - 1));
    if ((int16_t)rd16(h + 0x70) < 0) wr16(h + 0x70, 0);
    if ((int32_t)rd32(h + 0x6c) < 0) wr32(h + 0x6c, 0);
    /* lib_save / FUN_1800697e0 */
    if ((int32_t)rd32(h + 0x78) > 0) {   /* post_warning(0x1378): the delete above stays (Mastercam has written it already) */
        snprintf(msg, mn, "%s", S4984); return 2; }
    int32_t id = 1; uint64_t e20 = 0;
    for (int i = 0; i < m->nseg; i++) {
        const McwSeg *s = &m->seg[i]; const uint8_t *e = w + s->off + 4;
        if (s->kind != SEG_ENT || s->len < 4 + 0x80) continue;
        if (!(rd16(e + 4) & 1)) continue;
        uint64_t v = rd64(e + 0x20); if (v && (!e20 || e20 < v)) e20 = v;
        if (s->cls == 0x200 && s->sub == 0x0d && (int32_t)rd32(e + 0x78) >= 0) break;
        if ((int32_t)rd32(e + 0x2c) == 1 && id <= (int32_t)rd32(e + 0x68)) id = (int32_t)rd32(e + 0x68) + 1;
    }
    uint8_t ent[OP_ENT]; memset(ent, 0, sizeof ent);
    wr32(ent, 0x4000); wr32(ent + 0x2c, 1); wr16(ent + 4, 0x101);
    memcpy(ent + 0x68, rec + 0x68, OP_ENT - 0x68);
    wr32(ent + 0x68, (uint32_t)id);
    wr32(h + 0x7c, rd32(h + 0x7c) + 1); memcpy(ent + 0x34, h + 0x7c, 4);
    wr64(ent + 0x20, e20 + 0x30);
    ent[0x48] = h[0x48]; ent[0x49] = h[0x49]; memcpy(ent + 0x4b, h + 0x4b, 4);
    size_t pos = wn;
    if ((int32_t)rd32(h + 0x74) < 0) wr32(h + 0x74, (uint32_t)pos);
    w = xrealloc(w, wn + 4 + OP_ENT);
    wr32(w + pos, OP_ENT); memcpy(w + pos + 4, ent, OP_ENT);
    *W = w; *WN = wn + 4 + OP_ENT; *OFF = pos + 4;
    return 1;
}

static int dfl_last_written;                           /* tests: the record offset of the last rewrite in the working copy (-1 none) */
static long dfl_last_off = -1;
/* OnWriteToDefaultsPb + WriteOperationToDefaultsFile on the dialog's op. 1 = written (5419 shown) */
int pp_save_defaults(void) {
    if (!show_params) return 0;
    char real[1100]; grp_defaults_file(real, sizeof real);
    if (!real[0]) return 0;                                           /* OMgetGrpDefaultsFile: none -> false, no message */
    CamOp dop; pp_dialog_op(&dop);
    if (cam_type_is_3d(dop.p.type)) { cui_message("apprenticeCAM", "Save parameters to defaults file: 3D operations are not written to Mastercam files here (NOT DECODED) - nothing was written."); return 0; }
    Dfl d; if (!dfl_paths(&d, real)) return 0;
    if (!strcmp(d.work, real)) { cui_message("apprenticeCAM", "Save parameters to defaults file: the program's defaults folder is Mastercam's own folder - nothing was written (ours: paths.conf defaults)."); return 0; }
    char rd[1700], msg[1200], err[600] = "";
    if (!is_file(real) && !is_file(d.work)) {
        /* file_exists fails: init_operation_db + oplib_save into a new file - NOT DECODED */
        cui_message("apprenticeCAM", "Save parameters to defaults file: %s does not exist; creating a defaults file is not decoded - nothing was written.", real); return 0; }
    snprintf(rd, sizeof rd, "%s", is_file(d.work) ? d.work : real);
    McwFile m;
    if (!mcw_open(&m, rd, err, sizeof err)) { cui_message("apprenticeCAM", "Save parameters to defaults file: %s", err); return 0; }
    if (!version_ok(&m, msg, sizeof msg)) { cui_message("Mastercam", "%s", msg); mcw_close(&m); return 0; }
    /* the dialog op's record: its own (an edited Mastercam op) or the file's op of its type (an op made here) */
    uint8_t rec[OP_ENT], tmpl[OP_ENT]; int own = 0;
    {   int style, oc = mcam_opcode_for(&dop.p, &style);
        uint8_t probe[OP_ENT]; memset(probe, 0, sizeof probe); probe[0x6c] = (uint8_t)oc; probe[0x6d] = (uint8_t)(oc >> 8);
        if (oc == 0x86) probe[0x68 + 0x11c0] = (uint8_t)style;
        int si = oc ? match_op(&m, probe) : -1;
        if (dop.p.file_op_id > 0) {
            const Op *so = NULL; for (int i = 0; i < F.nops; i++) if (F.ops[i].id == dop.p.file_op_id && F.ops[i].raw && F.ops[i].raw_n >= OP_ENT) so = &F.ops[i];
            McwFile pm; int got = 0;
            if (so) { memcpy(tmpl, so->raw, OP_ENT); got = 1; }
            else if (F.path[0] && mcw_open(&pm, F.path, err, sizeof err)) {   /* taken over here: the part's stored record */
                for (int i = 0; i < pm.nseg && !got; i++) if (pm.seg[i].kind == SEG_ENT && pm.seg[i].cls == 0x4000 && pm.seg[i].sub == 1 && pm.seg[i].len == 4 + OP_ENT
                    && (int32_t)rd32(pm.d + pm.seg[i].off + 4 + 0x68) == dop.p.file_op_id) { memcpy(tmpl, pm.d + pm.seg[i].off + 4, OP_ENT); got = 1; }
                mcw_close(&pm);
            }
            if (!got) { cui_message("apprenticeCAM", "Save parameters to defaults file: the operation's stored record was not found - nothing was written."); mcw_close(&m); return 0; }
            own = 1;
            /* a stored op of an X9 part: Mastercam holds it as its X9 reader converted it (mcam_x9_op_update, mcprvverfile.dll
               FUN_18005e7f0), op version 19.1 kept -> the delete + append branch below when the defaults file's op is 20.0 */
            if (F.major == 0x12 && !mcam_x9_op_update(tmpl, mcam_is_metric(&F))) {
                cui_message("apprenticeCAM", "Save parameters to defaults file: this X9 operation's update to Mastercam 2017 (GetUpdatedOpcodeMW) is not decoded - nothing was written.");
                mcw_close(&m); return 0; }
        } else if (si >= 0) memcpy(tmpl, m.d + m.seg[si].off + 4, OP_ENT);
        if (si < 0) {   /* no op of the type: init_operation_db + oplib_save (append) - NOT DECODED */
            cui_message("apprenticeCAM", "Save parameters to defaults file: %s holds no operation of this type; adding one (Mastercam's oplib_save) is not decoded - nothing was written.", rd);
            mcw_close(&m); return 0; }
        if (!mcam_op_dialog_record(&F, &dop, tmpl, own, rec, msg, sizeof msg)) { cui_message("apprenticeCAM", "Save parameters to defaults file: %s - nothing was written.", msg); mcw_close(&m); return 0; }
        const uint8_t *se = m.d + m.seg[si].off + 4;
        /* the fields WriteOperationToDefaultsFile takes from the file's op: id (+0), +0x80 .. +0xf7, +0x28c */
        memcpy(rec + 0x68, se + 0x68, 4);
        memcpy(rec + 0x68 + 0x80, se + 0x68 + 0x80, 0x78);
        memcpy(rec + 0x68 + 0x28c, se + 0x68 + 0x28c, 4);
        /* same op struct version (and for 0x84 / 9 / 10 / 0xf / 0x2e / 0x83 / 0x6b the short +0x15e6 / +0x1282: not these opcodes)
           -> oplib_rewrite; else oplib_delete + oplib_save - NOT DECODED */
        int append = rdd(rec + 0x1218) != rdd(se + 0x1218);   /* -> oplib_delete + oplib_save (dfl_delete_append) */
        /* lib_rewrite: the body (ent +0x68, 0x28d0 bytes) replaced in the file's own record, its header kept; write_ent_to_disk ->
           FUN_1802c72c0 stamps +0x48 / +0x49 / +0x4b..+0x4e (ours: from the file's header record, as mcam_saveops.c) */
        uint8_t ent[OP_ENT]; memcpy(ent, se, OP_ENT); memcpy(ent + 0x68, rec + 0x68, OP_ENT - 0x68);
        for (int i = 0; i < m.nseg; i++) if (m.seg[i].kind == SEG_ENT && m.seg[i].cls == 0x200 && m.seg[i].sub == 0x0d && m.seg[i].len >= 4 + 0x80) {
            const uint8_t *h = m.d + m.seg[i].off + 4; ent[0x48] = h[0x48]; ent[0x49] = h[0x49]; memcpy(ent + 0x4b, h + 0x4b, 4); break; }
        size_t off = m.seg[si].off + 4;
        /* the working copy: pristine + working copy + a dated backup first (ours), then the record in place */
        char bname[200];
        if (!start_write(&d, bname, sizeof bname, err, sizeof err)) { cui_message("apprenticeCAM", "Save parameters to defaults file: %s - nothing was written.", err); mcw_close(&m); return 0; }
        size_t wn; uint8_t *w = read_all(d.work, &wn);
        if (!w || wn != m.n || memcmp(w + off - 4, m.d + off - 4, 4 + 0x68)) {   /* the copy must be the file just read */
            free(w); cui_message("apprenticeCAM", "Save parameters to defaults file: the working copy %s changed while saving - nothing was written.", d.work); mcw_close(&m); return 0; }
        int warn = 0;
        if (append) {
            int r = dfl_delete_append(&m, si, rec, &w, &wn, &off, msg, sizeof msg);
            if (!r) { free(w); mcw_close(&m); cui_message("apprenticeCAM", "Save parameters to defaults file: %s - nothing was written.", msg); return 0; }
            warn = r == 2;
        } else memcpy(w + off, ent, OP_ENT);
        int ok = write_all(d.work, w, wn, err, sizeof err);
        free(w); mcw_close(&m);
        if (!ok) { cui_message("apprenticeCAM", "Save parameters to defaults file: %s", err); return 0; }
        if (warn) { readers_reset(); cui_message("Mastercam", "%s", msg); return 0; }   /* post_warning(mccoreres 4984) after the delete */
        dfl_last_off = (long)off; dfl_last_written = 1;
    }
    readers_reset();                                                  /* ours: the cached copies read again */
    cui_message("apprenticeCAM", "%s", S5419);   /* message_box_ok(mccoreres 5419) */
    return 1;
}
/* the toolbar enable (ours: Mastercam's button is always enabled; OMgetGrpDefaultsFile none -> the click does nothing) */
int pp_save_defaults_live(void) { char real[1100]; grp_defaults_file(real, sizeof real); return real[0] && !cam_type_is_3d(pending.p.type); }

/* ---------------------------------------------------------------------------------------------- ours: reset / restore */
DflQuestion dfl_q;
static int dfl_target(Dfl *d) { char real[1100]; grp_defaults_file(real, sizeof real); return real[0] && dfl_paths(d, real); }
int dfl_has_copy(void) { Dfl d; return dfl_target(&d) && is_file(d.work); }
int dfl_backup_names(char (*names)[160], int max) { Dfl d; return dfl_target(&d) ? list_backups(&d, names, max) : 0; }
const char *dfl_work_path(char *out, size_t n) { Dfl d; out[0] = 0; if (dfl_target(&d)) snprintf(out, n, "%s", d.work); return out; }
/* "Reset defaults file to original (ours)": asks first (dfl_q) */
int dfl_reset_ask(void) {
    Dfl d; memset(&dfl_q, 0, sizeof dfl_q);
    if (!dfl_target(&d) || !is_file(d.pristine) || !is_file(d.work)) { cui_message("apprenticeCAM", "Reset defaults file to original (ours): this program has not changed the defaults file - nothing to reset."); return 0; }
    dfl_q.pending = DFLQ_RESET;
    snprintf(dfl_q.text, sizeof dfl_q.text, "Reset the defaults file to the original? (apprenticeCAM command, not Mastercam's)\n\n"
             "%s\nis replaced by the original copy kept before the first save:\n%s\n\nThe current file is kept as a dated backup first.", d.work, d.pristine);
    return 1;
}
/* "Restore a dated backup (ours)": backup I (0 = the oldest, list_backups' order) */
int dfl_restore_ask(int i) {
    Dfl d; memset(&dfl_q, 0, sizeof dfl_q);
    char names[256][160]; int n = dfl_target(&d) ? list_backups(&d, names, 256) : 0;
    if (i < 0 || i >= n) { cui_message("apprenticeCAM", "Restore a dated backup (ours): no such backup."); return 0; }
    dfl_q.pending = DFLQ_RESTORE; snprintf(dfl_q.arg, sizeof dfl_q.arg, "%s", names[i]);
    snprintf(dfl_q.text, sizeof dfl_q.text, "Restore the defaults file from a dated backup? (apprenticeCAM command, not Mastercam's)\n\n"
             "%s\nis replaced by\n%s/%s\n\nThe current file is kept as a dated backup first.", d.work, d.bdir, names[i]);
    return 1;
}
int dfl_answer(int yes) {
    int k = dfl_q.pending; dfl_q.pending = 0;
    if (!k || !yes) return 0;
    Dfl d; if (!dfl_target(&d)) return 0;
    char from[1800], bname[200], err[600] = "";
    if (k == DFLQ_RESET) snprintf(from, sizeof from, "%s", d.pristine); else path_join(from, sizeof from, d.bdir, dfl_q.arg);
    if (!is_file(from)) { cui_message("apprenticeCAM", "%s was not found - nothing was changed.", from); return 0; }
    size_t n; uint8_t *b = read_all(from, &n);                         /* read before the backup: a backup may be the source */
    if (!b) { cui_message("apprenticeCAM", "cannot read %s - nothing was changed.", from); return 0; }
    int ok = (!is_file(d.work) || backup_now(&d, bname, sizeof bname, err, sizeof err)) && write_all(d.work, b, n, err, sizeof err);
    free(b);
    if (!ok) { cui_message("apprenticeCAM", "%s - nothing was changed.", err); return 0; }
    readers_reset();
    cui_message("apprenticeCAM", k == DFLQ_RESET ? "The defaults file was reset to the original (ours)." : "The defaults file was restored from the backup (ours).");
    return 1;
}

/* ---------------------------------------------------------------------------------------------- tests (ours) */
/* dfldump: the working copy / pristine / backups (count, sizes - not their dated names), the bytes in which the working copy
   differs from the pristine copy (offset ranges relative to the last rewritten record's ent), and the working copy read back by
   this program's loader (mcam_load: op count, the op at the last rewrite's offset) */
static void dfl_dump(void) {
    Dfl d; if (!dfl_target(&d)) { printf("    dfl: no defaults file\n"); return; }
    char names[256][160]; int nb = list_backups(&d, names, 256);
    printf("    dfl: work %d pristine %d backups %d (%s)\n", is_file(d.work), is_file(d.pristine), nb, d.name);
    for (int i = 0; i < nb; i++) { char p[1800]; path_join(p, sizeof p, d.bdir, names[i]); size_t n = 0; uint8_t *b = read_all(p, &n);
        size_t pn = 0; uint8_t *pb = read_all(d.pristine, &pn);
        printf("    dfl: backup %d: %zu bytes%s\n", i + 1, n, b && pb && n == pn && !memcmp(b, pb, n) ? ", = pristine" : ""); free(b); free(pb); }
    size_t wn = 0, pn = 0, rn = 0; uint8_t *w = read_all(d.work, &wn), *pr = read_all(d.pristine, &pn), *rl = read_all(d.real, &rn);
    if (pr && rl) printf("    dfl: pristine %s the original\n", pn == rn && !memcmp(pr, rl, pn) ? "=" : "!=");
    if (w && pr) {
        if (wn != pn) {
            printf("    dfl: work %zu bytes, pristine %zu\n", wn, pn);
            if (wn > pn) {   /* delete + append: the changed bytes of the old part (file offsets), the records appended */
                int runs = 0; size_t i = 0, tot = 0;
                printf("    dfl: diff vs pristine in its %zu bytes (file offsets):", pn);
                while (i < pn) { if (w[i] == pr[i]) { i++; continue; } size_t j = i; while (j < pn && w[j] != pr[j]) j++;
                    if (runs < 40) printf(" %zu..%zu", i, j); runs++; tot += j - i; i = j; }
                printf("%s (%zu bytes in %d runs)\n", runs ? "" : " none", tot, runs);
                for (size_t o = pn; o + 4 + OP_ENT <= wn && rd32(w + o) == OP_ENT; o += 4 + OP_ENT) {
                    const uint8_t *e = w + o + 4;
                    printf("    dfl: appended at %zu: class 0x%x assoc %d sel 0x%x +0x20 0x%llx idn %d stamp %02x %02x %02x%02x%02x%02x id %d opcode %d version %g\n",
                           o, rd32(e), (int)rd32(e + 0x2c), rd16(e + 4), (unsigned long long)rd64(e + 0x20), (int)rd32(e + 0x34), e[0x48], e[0x49],
                           e[0x4b], e[0x4c], e[0x4d], e[0x4e], (int)rd32(e + 0x68), (int16_t)rd16(e + 0x6c), rdd(e + 0x1218));
                }
            }
        } else {
            long base = dfl_last_off >= 0 ? dfl_last_off : 0; int runs = 0; size_t i = 0, tot = 0;
            printf("    dfl: diff vs pristine (ent offsets):");
            while (i < wn) { if (w[i] == pr[i]) { i++; continue; } size_t j = i; while (j < wn && w[j] != pr[j]) j++;
                if (runs < 40) printf(" %+ld..%+ld", (long)i - base, (long)j - base); runs++; tot += j - i; i = j; }
            printf("%s (%zu bytes in %d runs)\n", runs ? "" : " none", tot, runs);
            if (runs && dfl_last_off >= 0) { int inside = 1; for (size_t k = 0; k < wn; k++) if (w[k] != pr[k] && ((long)k < dfl_last_off + 0x68 || (long)k >= dfl_last_off + OP_ENT)) inside = 0;
                printf("    dfl: every changed byte inside the rewritten op body: %s\n", inside ? "yes" : "NO"); }
        }
    }
    free(w); free(pr); free(rl);
    if (is_file(d.work)) { McamFile f; if (mcam_load(&f, d.work)) { printf("    dfl: work read back: %d ops\n", f.nops); mcam_free(&f); } else printf("    dfl: work read back: FAILED\n"); }
}
int cui_defaults_script(const char *st, const char *arg) {
    #define IS(S_) (!strcmp(st, S_))
    if (IS("dfldir") && arg) {                         /* the program's defaults folder for this run (paths.h: MCAMUI_DEFAULTS) */
#ifdef _WIN32
        _putenv_s("MCAMUI_DEFAULTS", arg);
#else
        setenv("MCAMUI_DEFAULTS", arg, 1);
#endif
        readers_reset(); return 1; }
    if (IS("dflsave")) { if (show_params) pp_save_defaults(); return 1; }
    if (IS("dflreset")) { dfl_reset_ask(); return 1; }
    if (IS("dflrestore") && arg) { dfl_restore_ask(atoi(arg) - 1); return 1; }
    if (IS("dflyes") || IS("dflno")) { if (!dfl_q.pending) printf("    (no question)\n"); else dfl_answer(IS("dflyes")); return 1; }
    if (IS("dfldump")) { dfl_dump(); return 1; }
    #undef IS
    return 0;
}
