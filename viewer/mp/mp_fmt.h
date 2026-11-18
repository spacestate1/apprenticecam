/* mp_fmt - MP number formatting: `fs2` / `fs` format statements and number -> text (port of mcpost.dll CPostFs::ParseFs2
 * @180098130, CPostFs::MakeFs2String / MakeFormatNumber, CPostMath::FormattedNumber @1800bc9d0 / @1800bcd40,
 * CPostMath::FormattedNumberString @1800bcd70, CPostMath::DressedFormattedNumberString @1800bb9a0, post_round, RoundFcvtBuffer).
 * Contract shared by the interpreter (mp_run*.c) and the formatter (mp_fmt.c). See ../../formats/mp_engine.md. */
#ifndef MP_FMT_H
#define MP_FMT_H
#include <stddef.h>

typedef struct {
    int defined;            /* 1 once a format statement set it */
    int idig[2], ddig[2];   /* integer / decimal digit counts: [0] inch, [1] metric */
    unsigned flags;         /* CPostFs::ParseFs2 option bits (+ 0x1, s 0x2, sep 0x04..0xc0, l 0x100, t 0x200, n 0x400,
                               i 0x1000, d 0x2000, z 0x4000, ...): exact meaning documented in mp_fmt.c */
    /* --- added by mp_fmt (fields of strt_fs, see CPostFs::AddStatement @180097870) --- */
    int num;                /* fs number (strt_fs +0x08) */
    int round[2];           /* round digit after the dec digit ("0.45" -> 5): 0,2,5,9 (strt_fs +0x12 / +0x14) */
    double maxv[2];         /* CPostFs::SetMaxFormatNumber @180098e00 (strt_fs +0x18 / +0x20); no reader found */
    unsigned msg;           /* CPostMessage type bits ParseFs2 raised (MP_FS_MSG_*); bit 0 = error */
} MpFs;

/* MpFs.flags bits (CPostFs::ParseFs2 @180098130) */
#define MP_FS_PLUS      0x0001u  /* '+' : '+' before positive numbers */
#define MP_FS_SPACE     0x0002u  /* 's' : ' ' before positive numbers (only if no '+') */
#define MP_FS_PT_IN     0x0004u  /* inch: '.' separator */
#define MP_FS_CM_IN     0x0008u  /* inch: ',' separator */
#define MP_FS_FRAC_IN   0x0010u  /* inch: separator only when there is a fraction ('^' '%') */
#define MP_FS_PT_MM     0x0020u  /* metric: '.' */
#define MP_FS_CM_MM     0x0040u  /* metric: ',' */
#define MP_FS_FRAC_MM   0x0080u  /* metric: separator only with a fraction */
#define MP_FS_LEAD      0x0100u  /* 'l' : leading zeros to idig */
#define MP_FS_TRAIL     0x0200u  /* 't' : trailing zeros to ddig */
#define MP_FS_NONMODAL  0x0400u  /* 'n' : (interpreter) forced output */
#define MP_FS_INCR      0x1000u  /* 'i' : output current - previous (DressedFormattedNumberString, types 900/901) */
#define MP_FS_DELTA     0x2000u  /* 'd' : (interpreter) delta */
#define MP_FS_ZERO      0x4000u  /* 'z' : "0" after a separator with no fraction */

/* ParseFs2 message bits (CPostMessage::OrTypeMessage). Errors have bit 0 set: the statement is not added. */
#define MP_FS_MSG_NOARG     0x21u    /* nothing after the keyword */
#define MP_FS_MSG_BADNUM    0x41u    /* fs number missing or <= 0 */
#define MP_FS_MSG_BADIINT   0x81u    /* inch integer digit */
#define MP_FS_MSG_BADIDEC   0x101u   /* inch decimal digit */
#define MP_FS_MSG_BADMINT   0x401u   /* metric integer digit */
#define MP_FS_MSG_BADMDEC   0x801u   /* metric decimal digit */
#define MP_FS_MSG_IRND      0x202u   /* warning: inch round digit not in "0259" (set to 0) */
#define MP_FS_MSG_MRND      0x1002u  /* warning: metric round digit not in "0259" (set to 0) */
#define MP_FS_MSG_ID        0x2002u  /* warning: both 'd' and 'i' ('i' ignored) */
#define MP_FS_MSG_Z         0x4002u  /* warning: 'z' with '^'/'%' ('z' cleared) */
#define MP_FS_MSG_DUP       0x8000u  /* warning: an option letter given more than once */
#define MP_FS_MSG_TRAIL     0x10000u /* warning: unparsed text left (statement still added) */

/* round option state the formatter needs from post variables (round_opt$ etc.; see CPostMath::UnAssembleRoundOption) */
typedef struct {
    double round_opt;       /* value of round_opt$ (0 = default _fcvt rounding) */
} MpFmtCtx;

/* parse one `fs2 N ...` / `fs N ...` declaration line (comment already stripped or not); *num = N. 1 ok, 0 error (err) */
int mp_fs_parse(const char *line, int *num, MpFs *fs, char *err, size_t errn);

/* the value rounded to the format's decimal places (what fmtrnd() returns and what modality compares) */
double mp_fmt_round(const MpFs *fs, int metric, double v, const MpFmtCtx *cx);

/* number text without prefix/suffix (FormattedNumberString). returns length */
int mp_fmt_number(const MpFs *fs, int metric, double v, const MpFmtCtx *cx, char *out, size_t n);

/* prefix + number + suffix as output to the NC line (DressedFormattedNumberString, non-incremental part). returns length */
int mp_fmt_dressed(const MpFs *fs, int metric, const char *prefix, const char *suffix, double v, const MpFmtCtx *cx,
                   char *out, size_t n);

/* ---- additions (mp_fmt) ---- */
/* like mp_fs_parse; *msg receives the message bits (warnings too). 1 ok, 0 error */
int mp_fs_parse2(const char *line, int *num, MpFs *fs, unsigned *msg, char *err, size_t errn);

/* CPostFs::SetMaxFormatNumber @180098e00: fills fs->maxv[] (mp_fs_parse already calls it) */
void mp_fs_set_max(MpFs *fs);

/* CPostMath::FormattedNumberString @1800bcd70 with all its inputs:
     opt MP_FMT_ROUND: round first (FormattedNumber @1800bc9d0); without it only ValidateNumber @1800c1bd0 is applied
                       (the OutputVariable path rounds with FormattedNumber, stores, then formats with round=0)
     opt MP_FMT_RAW  : debug "param_6" mode (20 int / 9 dec digits, '.' always, l/t ignored; sign options kept)
   *ret (may be NULL) = the double FormattedNumberString returns; *ovf (may be NULL) = ValidateNumber overflow (>1e20). */
#define MP_FMT_ROUND 1
#define MP_FMT_RAW   2
int mp_fmt_number_ex(const MpFs *fs, int metric, double v, const MpFmtCtx *cx, int opt, char *out, size_t n,
                     double *ret, int *ovf);

/* value DressedFormattedNumberString @1800bb9a0 formats for post-table types 900/901: cur - prev when the fs has
   MP_FS_INCR ('i'), else cur. (Other types: cur.) */
double mp_fmt_incr_value(const MpFs *fs, double cur, double prev);

/* MSVCR120 _fcvt_s emulation used by the formatter (exposed for tests). returns errno-style code */
int mp_ms_fcvt_s(char *buf, size_t sz, double v, int ndec, int *decpt, int *sign);
#endif
