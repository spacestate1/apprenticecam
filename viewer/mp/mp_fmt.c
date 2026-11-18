/* mp_fmt.c - MP number formatting, port of mcpost.dll (Mastercam 2017):
 *
 *   CPostFs::ParseFs2                    @180098130  `fs2` / `fs` statement parser
 *   CPostFs::ParseFs2AddStatement        @180098ae0  (both `fs` and `fs2` reach ParseFs2 through this; only the
 *                                                     message flag differs, 0x20000 vs 0x40000 - CPostParse @~180064460)
 *   CPostFs::SetMaxFormatNumber          @180098e00
 *   CPostMath::ValidateNumber            @1800c1bd0
 *   CPostMath::FormattedNumber           @1800bc9d0  (rounding; what fmtrnd()/modality use)
 *   CPostMath::FormattedNumberString     @1800bcd70  (text)
 *   CPostMath::RoundFcvtBuffer           @1800bfbd0  (_fcvt_s + round_opt$ digit rounding)
 *   CPostMath::UnAssembleRoundOption     @1800c1b50
 *   CPostMath::round_long                @1800c82c0
 *   CPostMath::DressedFormattedNumberString @1800bb9a0 (prefix + number + suffix, incremental value)
 * Sources: decomp/mcpost_format.c, decomp/mcpost_numfmt.c, ~/mastercam-re/work/mp/mcpost_all.c; the string constants
 * were read from mcpost.dll (.rdata): 0x18020dc24 " ", 0x180216608 "+", 0x1802161c8 "-", 0x18020fea8 ".",
 * 0x180216654 ",", 0x1802151b0 "0", 0x18021663c ".,^%", 0x180216644 "0259", option sets "sS" "lL" "tT" "nN" "dD"
 * "iI" "zZ", removal list "+SLTNDIZ" (ParseFs2 local_58 = 0x5a49444e544c532b). The FormattedNumberString call whose
 * argument Ghidra dropped (both int and fraction empty) was read from the disassembly at 1800bd1a8: it appends "0".
 *
 * The DLL rounds with MSVCR120 `_fcvt_s`. That CRT routine is reproduced in mp_ms_fcvt_s() below from the
 * disassembly of <windows>/Windows/System32/msvcr120.dll (export _fcvt_s @18003e694 -> _fltout2 @18007d134 ->
 * $I10_OUTPUT(ld, 17, 0) @18007e848 -> _fpcvt @18003e3a4 -> _fptostr @1800251e0):
 *   1. $I10_OUTPUT produces 18 significant decimal digits of the value (r15 = ndigits+1 at 18007f13a), then rounds
 *      to 17 digits HALF-UP on the 18th digit (cmp al,0x35 at 18007f22b; 9s carry), then strips trailing zeros.
 *   2. _fptostr copies decpt+ndec digits of that 17-digit mantissa (padding '0'), and rounds HALF-UP on the next
 *      mantissa digit only (cmp [rcx],0x35 at 180025268), carry into a leading '0' slot (decpt++ if it became '1').
 * So the rounding is a DOUBLE rounding on decimal text (17 significant digits first), NOT a rounding of the exact
 * binary value: e.g. 1.005 (binary 1.00499999999999989...) -> 17 digits "1005" -> _fcvt(…,2) = "101" (1.01), where a
 * correctly-rounded printf gives 1.00. Ours: step 1 is computed from the exact binary expansion (glibc printf with
 * enough precision) where $I10_OUTPUT uses a 96-bit scaled approximation; the two can only differ when digits
 * 18.. of the exact value are an exact tie / within ~1e-28 relative of one, and then only matter if digits
 * ndec+1..17 are "4999..9". Documented as residual uncertainty, not observed in any oracle file.
 *
 * Everything else is a line-by-line port; deviations / guards that are ours are marked "ours". */
#include "mp_fmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>

/* ------------------------------------------------------------------------------------------------------------ */
/* MSVCR120 number -> digits                                                                                     */
/* ------------------------------------------------------------------------------------------------------------ */

/* $I10_OUTPUT(ld, 17, 0, &fos) as used by _fltout2: man = up to 17 significant digits (trailing zeros stripped),
 * *exp = decimal point position (value = 0.man * 10^exp), *neg = sign bit. returns 0 for inf/nan (not decoded). */
static int i10_output17(double v, char man[24], int *exp, int *neg)
{
    *neg = signbit(v) ? 1 : 0;
    if (v == 0.0) {                       /* 18007e8fc: man "0", ManLen 1, exp 0 */
        strcpy(man, "0");
        *exp = 0;
        return 1;
    }
    if (!isfinite(v)) {                   /* NOT DECODED: $I10_OUTPUT special values ("1#INF"/"1#QNAN" path) */
        man[0] = 0;
        *exp = 0;
        return 0;
    }
    /* exact decimal expansion of the binary value (ours: glibc printf is exact at this precision; a double has at
       most 767 significant digits) */
    char big[1100];
    snprintf(big, sizeof big, "%.780e", fabs(v));
    char dig[800];
    int nd = 0;
    const char *p = big;
    for (; *p && *p != 'e'; p++)
        if (*p >= '0' && *p <= '9' && nd < (int)sizeof dig - 1) dig[nd++] = *p;
    dig[nd] = 0;
    int e10 = atoi(p + 1) + 1;            /* 0.d1d2.. * 10^e10 */
    /* 18 generated digits (truncated), round on the 18th */
    char g[19];
    for (int i = 0; i < 18; i++) g[i] = i < nd ? dig[i] : '0';
    g[18] = 0;
    int last = 17;
    if (g[17] >= '5') {                   /* 18007f22b: half-up, '9' -> '0' carry */
        int i = 16;
        while (i >= 0 && g[i] == '9') g[i--] = '0';
        if (i < 0) {                      /* carried past the first digit: "1", exp+1 */
            memmove(g + 1, g, 17);
            g[0] = '1';
            e10++;
        } else
            g[i]++;
    }
    g[last] = 0;                          /* 17 digits */
    int L = 17;
    while (L > 1 && g[L - 1] == '0') g[--L] = 0;   /* 18007f290: strip trailing zeros */
    memcpy(man, g, (size_t)L + 1);
    *exp = e10;
    return 1;
}

/* _fcvt_s(buf, sz, value, ndec, &decpt, &sign) of MSVCR120 (see top comment). returns 0 / 22 / 34 */
int mp_ms_fcvt_s(char *buf, size_t sz, double v, int ndec, int *decpt, int *sign)
{
    if (!buf || sz == 0 || !decpt || !sign) return 22;   /* EINVAL (18003e6d2) */
    buf[0] = 0;
    char man[24];
    int e10, neg;
    if (!i10_output17(v, man, &e10, &neg)) return 22;   /* NOT DECODED (inf/nan); ours: report EINVAL */
    long digits = (long)e10 + ndec;                      /* 18003e718 */
    if (ndec > 0 && e10 > 0 && digits < ndec) digits = INT_MAX;
    /* _fpcvt 18003e3a4: size check, clamp */
    long need = (digits > 0 ? digits : 0) + 2;
    if ((long)sz < need) return 34;                      /* ERANGE; decpt/sign not written */
    if (digits > (long)sz - 2) digits = (long)sz - 2;
    /* _fptostr 1800251e0 */
    char *b = buf, *m = man;
    long d = digits;
    *b++ = '0';
    while (d > 0) {
        *b++ = *m ? *m++ : '0';
        d--;
    }
    *b = 0;
    if (d >= 0 && *m >= '5') {
        b--;
        while (*b == '9') *b-- = '0';
        *b += 1;
    }
    if (buf[0] == '1')
        e10++;
    else
        memmove(buf, buf + 1, strlen(buf + 1) + 1);
    *sign = neg;                                         /* 18003e409: sign == '-' */
    *decpt = e10;
    return 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* CPostMath helpers                                                                                              */
/* ------------------------------------------------------------------------------------------------------------ */

/* 10^n the way FormattedNumber / SetMaxFormatNumber compute it (square-and-multiply, 1/x for n < 0) */
static double pow10_sq(int n)
{
    unsigned u = n < 0 ? (unsigned)-n : (unsigned)n;
    double b = 10.0, r = 1.0;
    for (;;) {
        if (u & 1) r *= b;
        u >>= 1;
        if (!u) break;
        b *= b;
    }
    return n < 0 ? 1.0 / r : r;
}

/* CPostMath::round_long @1800c82c0. (int) of an out-of-range double: x64 cvttsd2si gives INT_MIN (ours: explicit) */
static long round_long(double x)
{
    double r = x < 0.0 ? ceil((x - 0.5) - 1e-12) : floor(x + 0.5 + 1e-12);
    if (!(r > -2147483649.0 && r < 2147483648.0)) return INT_MIN;
    return (int)r;
}

/* CPostMath::ValidateNumber @1800c1bd0 with param_5 = 0 (the only use in the formatter) */
static double validate_number(double v, int *ovf)
{
    double lim = 1e20;
    if (ovf) *ovf = 0;
    if (lim < v) { if (ovf) *ovf = 1; return lim; }
    if (v < -lim) { if (ovf) *ovf = 1; return -lim; }
    if (fabs(v) <= 1e-20) return 0.0;
    return v;
}

/* CPostMath::UnAssembleRoundOption @1800c1b50: returns the tens digit (param_3) of round_opt$ */
static short round_opt_tens(long r)
{
    int t = (int)((r % 1000) % 100);
    return (short)(t / 10);
}

static long round_opt_of(const MpFmtCtx *cx)
{
    double r = cx ? cx->round_opt : 0.0;
    /* GetVarRef(var_round_opt_) then (int) - truncation toward zero */
    if (!(r > -2147483649.0 && r < 2147483648.0)) return INT_MIN;
    return (int)r;
}

/* CPostMath::RoundFcvtBuffer @1800bfbd0. b must hold 0x15d bytes. mode = round_opt$ digit (1/2 = digit rounding) */
static int round_fcvt_buffer(char *b, double v, int nd, int *dec, int *sign, short mode)
{
    if (nd < 0) nd = 0;
    int digits_req = ((unsigned short)(mode - 1) <= 1) ? 25 : nd;
    int e = mp_ms_fcvt_s(b, 0x15d, v, digits_req, dec, sign);
    int d = *dec;
    if (d < -20) {                        /* d < 0x27 always true here */
        b[0] = '1'; b[1] = 0; *dec = -20;
        return 5;
    }
    if (d >= 39) {
        b[38] = 0;
        for (int k = 37; k > 0; k--) b[k] = '0';
        b[0] = '1'; *dec = 38;
        return 5;
    }
    if ((unsigned short)(mode - 1) > 1) return e;
    if (e) return e;
    /* strip trailing zeros (keeps the integer digits and the first fraction digit when dec >= 1) */
    int len = (int)strlen(b);
    int i = len - 1;
    for (;;) {
        int dd = *dec;
        int c = dd < 1 ? (i >= 0) : (dd < i);
        if (!c || b[i] != '0') break;
        b[i] = 0;
        i--;
    }
    d = *dec;
    len = (int)strlen(b);
    if (len != 0) {
        if ((len - d) - nd < 1) return 0;
        int k = d + nd;                   /* index of the first dropped digit */
        if (k >= 0) {
            int first = 1;
            if (len <= k) return 4;       /* _MCAssert PPostMath.cpp 0x6a7 */
            if (b[k] != '4') {            /* only a '4' keeps the digits behind it (cascade) */
                b[k + 1] = 0;
                len = (int)strlen(b);
            }
            i = len - 1;
            char *p = b + i;
            int carry = '4' < *p;
            if (i >= 0) {
                do {
                    long pos = p - b;
                    if (pos < k) {        /* kept digit */
                        if (!carry) return 0;
                        char c = (char)(*p + 1);
                        carry = '9' < c;
                        if (carry) c = '0';
                        *p = c;
                        if (first) {
                            if (c == '0' && *dec <= i) { *p = 0; goto next; }
                            first = 0;
                        }
                    } else {              /* dropped digit */
                        if (carry) *p = (char)(*p + 1);
                        if (pos == k || mode == 1)
                            carry = (signed char)*p >= '5';
                        else
                            carry = (signed char)*p >= '9';
                        *p = 0;
                    }
                next:
                    p--;
                    i--;
                } while (p - b >= 0);
            }
            if (!carry) return 0;
            if (i == -1) {
                int L = (int)strlen(b);
                memmove(b + 1, b, (size_t)L + 1);
                b[0] = '1';
                (*dec)++;
            }
            return 0;
        }
    }
    b[0] = '0'; b[1] = 0; *dec = 0;
    return 0;
}

/* CPostMath::FormattedNumber @1800bc9d0 (do_round = the in/out char param_2) */
static double formatted_number(const MpFs *fs, int metric, double v, int do_round, const MpFmtCtx *cx, int *ovf)
{
    double d = validate_number(v, ovf);
    if (!do_round) return d;
    /* NOT DECODED: if |ddig| > 9 the DLL first copies the default format statement (components+0xe8) over this
       one; ParseFs2 reads single-digit counts so this cannot happen for parsed statements. */
    int nd = metric ? fs->ddig[1] : fs->ddig[0];
    int mode = metric ? fs->round[1] : fs->round[0];
    int ndig = nd;
    if (mode == 2 || mode == 5) {         /* round to steps of 2 / 5 units of the last digit */
        double s = pow10_sq(nd) / (double)mode;
        long l = round_long(d * s);
        d = (double)l / s;
    } else if (mode == 8)
        ndig = nd + 1;
    else if (mode == 9)
        ndig = 16;
    short r1 = round_opt_tens(round_opt_of(cx));
    char buf[0x15d + 8];
    int dec = 0, sign = 0;
    round_fcvt_buffer(buf, d, ndig, &dec, &sign, r1);   /* rc != 0 -> _MCAssert, continues */
    char s[0x78 + 8];
    s[0] = 0;
    if (dec < 0) {
        int z = -dec;
        if (z + 1 > 0x77) z = 0x76;       /* ours: the DLL aborts (__report_rangecheckfailure) */
        s[0] = '.';
        for (int k = 1; k <= z; k++) s[k] = '0';
        s[z + 1] = 0;
        if (nd > z) strncat(s, buf, (size_t)(nd - z));
    } else {
        if (dec != 0) {
            snprintf(s, 0x78, "%s", buf);
            if (dec < 0x78 && dec < (int)strlen(s)) s[dec] = 0;
        }
        strcat(s, ".");
        if (dec <= (int)strlen(buf)) strncat(s, buf + dec, (size_t)(nd > 0 ? nd : 0)); /* ours: guard */
    }
    /* atof: ours = glibc strtod (correctly rounded); MSVCR120 atof not verified bit-exact */
    double r = strtod(s, NULL);
    return sign ? -r : r;
}

/* CPostMath::FormattedNumberString @1800bcd70 */
static int formatted_number_string(const MpFs *fs, int metric, double v, const MpFmtCtx *cx, int do_round, int raw,
                                   char *out, size_t n, double *ret, int *ovf)
{
    double d = do_round ? formatted_number(fs, metric, v, 1, cx, ovf) : validate_number(v, ovf);
    if (ret) *ret = d;
    int nint, ndec;
    unsigned fl = fs->flags;
    if (!raw) {
        nint = metric ? fs->idig[1] : fs->idig[0];
        ndec = metric ? fs->ddig[1] : fs->ddig[0];
    } else {
        nint = 20;
        ndec = 9;
    }
    char buf[0x15d + 8];
    int dec = 0, sign = 0;
    buf[0] = 0;
    if (isnan(d)) {                       /* NOT DECODED ($I10_OUTPUT NaN text); ours: empty output */
        if (out && n) out[0] = 0;
        return 0;
    }
    if (d == 0.0) {
        buf[0] = 0; dec = 0; sign = 0;
    } else {
        long ro = round_opt_of(cx);
        round_fcvt_buffer(buf, d, ndec, &dec, &sign, (short)(ro / 10));
        if (strlen(buf) == 0) { dec = 0; sign = 0; }
    }
    /* integer part */
    char ip[0x78 + 8];
    ip[0] = 0;
    if (dec > 0) {
        snprintf(ip, 0x78, "%s", buf);
        if (dec < 0x78 && dec < (int)strlen(ip)) ip[dec] = 0;
    }
    if ((fl & MP_FS_LEAD) && !raw) {      /* 'l': left-pad with '0' to nint */
        int il = (int)strlen(ip);
        if (il < nint && nint < 0x78) {
            memmove(ip + (nint - il), ip, (size_t)il + 1);
            memset(ip, '0', (size_t)(nint - il));
        }
    }
    /* fraction part (buffer of 0x28 in the DLL) */
    char fp[0x28 + 8];
    fp[0] = 0;
    if (dec < 0) {
        int z = -dec;
        if (z > 0x27) z = 0x27;           /* ours: DLL aborts */
        memset(fp, '0', (size_t)z);
        fp[z] = 0;
        if (z < ndec) strncat(fp, buf, (size_t)(ndec - z) < 0x27 - (size_t)z ? (size_t)(ndec - z) : 0x27 - (size_t)z);
    } else {
        if (dec <= (int)strlen(buf))      /* ours: guard */
            strncat(fp, buf + dec, (size_t)ndec < 0x27 ? (size_t)ndec : 0x27);
    }
    int fl_len = (int)strlen(fp);
    if (!(fl & MP_FS_TRAIL) || raw) {     /* no 't': strip trailing zeros */
        int k = fl_len;
        while (--k >= 0 && fp[k] == '0') fp[k] = 0;
    } else {                              /* 't': exactly ndec digits */
        if (ndec >= fl_len && ndec < 0x28) {
            memset(fp + fl_len, '0', (size_t)(ndec - fl_len));
        }
        if (ndec < 0x28) fp[ndec] = 0;
    }
    /* assemble */
    char o[0x200];
    o[0] = 0;
    if (sign)
        strcat(o, "-");
    else if (fl & MP_FS_PLUS)
        strcat(o, "+");
    else if (fl & MP_FS_SPACE)
        strcat(o, " ");
    int ilen = (int)strlen(ip), flen = (int)strlen(fp), s3;
    if (ilen == 0 && flen == 0) {         /* 1800bd1a8: appends "0" */
        strcat(o, "0");
        s3 = 1;
    } else {
        strcat(o, ip);
        s3 = ilen;
    }
    if (raw) {
        strcat(o, ".");
        strcat(o, fp);
    } else {
        unsigned pt = metric ? MP_FS_PT_MM : MP_FS_PT_IN;
        unsigned cm = metric ? MP_FS_CM_MM : MP_FS_CM_IN;
        unsigned fo = metric ? MP_FS_FRAC_MM : MP_FS_FRAC_IN;
        if (fl & pt) {
            if (flen || !(fl & fo)) { strcat(o, "."); goto L25d; }
            goto L263;
        } else if (fl & cm) {
            if (flen || !(fl & fo)) { strcat(o, ","); goto L25d; }
            goto L263;
        } else {
            /* no separator, 't' without 'l', empty integer part: drop leading zeros of the fraction (keep 1) */
            if ((fl & MP_FS_TRAIL) && !(fl & MP_FS_LEAD) && s3 == 0) {
                if (flen == 0) goto L263;
                int a = flen - 1;
                while (a > 0 && fp[0] == '0') {
                    memmove(fp, fp + 1, (size_t)a);
                    fp[a] = 0;
                    a--;
                }
            }
            goto L25d;
        }
    L25d:
        if (flen != 0) { strcat(o, fp); goto done; }
    L263:
        if (fl & MP_FS_ZERO) strcat(o, "0");
        else strcat(o, fp);
    }
done:
    if (out && n) snprintf(out, n, "%s", o);
    return (int)strlen(o);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* public                                                                                                        */
/* ------------------------------------------------------------------------------------------------------------ */

double mp_fmt_round(const MpFs *fs, int metric, double v, const MpFmtCtx *cx)
{
    return formatted_number(fs, metric ? 1 : 0, v, 1, cx, NULL);
}

int mp_fmt_number_ex(const MpFs *fs, int metric, double v, const MpFmtCtx *cx, int opt, char *out, size_t n,
                     double *ret, int *ovf)
{
    return formatted_number_string(fs, metric ? 1 : 0, v, cx, (opt & MP_FMT_ROUND) != 0, (opt & MP_FMT_RAW) != 0,
                                   out, n, ret, ovf);
}

int mp_fmt_number(const MpFs *fs, int metric, double v, const MpFmtCtx *cx, char *out, size_t n)
{
    /* = OutputVariable @1800dd320 path: FormattedNumber(round) then FormattedNumberString(round=0); identical to
       FormattedNumberString(round=1) because ValidateNumber is the identity on an already rounded value */
    return mp_fmt_number_ex(fs, metric, v, cx, MP_FMT_ROUND, out, n, NULL, NULL);
}

int mp_fmt_dressed(const MpFs *fs, int metric, const char *prefix, const char *suffix, double v, const MpFmtCtx *cx,
                   char *out, size_t n)
{
    /* DressedFormattedNumberString @1800bb9a0 (fmt record present): prefix string, then the number, then suffix */
    char num[0x200];
    mp_fmt_number(fs, metric, v, cx, num, sizeof num);
    char o[0x400];
    snprintf(o, sizeof o, "%s%s%s", prefix ? prefix : "", num, suffix ? suffix : "");
    if (out && n) snprintf(out, n, "%s", o);
    return (int)strlen(o);
}

double mp_fmt_incr_value(const MpFs *fs, double cur, double prev)
{
    /* DressedFormattedNumberString 1800bbbfd: for param_1 900/901 and fs flag 0x1000: value(+0x10) - prev(+0x18) */
    return (fs->flags & MP_FS_INCR) ? cur - prev : cur;
}

void mp_fs_set_max(MpFs *fs)
{
    for (int u = 0; u < 2; u++) {
        int ni = fs->idig[u], nd = fs->ddig[u], mode = fs->round[u];
        double m = 0.0;
        if (ni == 0) {
            fs->maxv[u] = 1e20;           /* 0x4415af1d78b58c40; the decimal part is then not added */
            continue;
        }
        for (int i = 0; i < ni; i++) m += pow10_sq(i) * 9.0;
        if (nd != 0) {
            int k = nd;
            if (mode == 2 || mode == 5) k--;
            int last = 1;
            if (k > 0) {
                for (int j = 1; j <= k; j++) m += pow10_sq(-j) * 9.0;
                last = k + 1;
            }
            if (mode == 2) m += pow10_sq(-last) * 8.0;
            else if (mode == 5) m += pow10_sq(-last) * 5.0;
        }
        fs->maxv[u] = m;
    }
}

/* --- ParseFs2 string helpers (MFC CStringT semantics) --- */
static void s_trim_left(char *s)
{
    size_t i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}
static void s_trim_right(char *s)
{
    size_t L = strlen(s);
    while (L && isspace((unsigned char)s[L - 1])) s[--L] = 0;
}
static void s_trim_left_set(char *s, const char *set)
{
    size_t i = 0;
    while (s[i] && strchr(set, s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}
static void s_delete(char *s, size_t at, size_t cnt)
{
    size_t L = strlen(s);
    if (at >= L) return;
    if (at + cnt > L) cnt = L - at;
    memmove(s + at, s + at + cnt, L - at - cnt + 1);
}
/* ParsePostLong on Left(1): one char, ok if it is a digit (ours: strtol of a lone '+'/'-' also fails) */
static int one_digit(const char *s, int *v)
{
    if (s[0] >= '0' && s[0] <= '9') { *v = s[0] - '0'; return 1; }
    *v = 0;
    return 0;
}

int mp_fs_parse2(const char *line, int *num, MpFs *fs, unsigned *msgp, char *err, size_t errn)
{
    unsigned msg = 0;
    char s[1024];
    MpFs f;
    memset(&f, 0, sizeof f);
    if (err && errn) err[0] = 0;
    snprintf(s, sizeof s, "%s", line ? line : "");
    if (!s[0]) { msg |= MP_FS_MSG_NOARG; goto out; }            /* ParseFs2AddStatement: IsEmpty -> 0x21 */
    char *h = strchr(s, '#');                                    /* Find('#') -> Left */
    if (h) *h = 0;
    /* CStringDeleteItemTrimToNextItem: FindOneOf(" \t\v") (0x18021f0b8), Delete(0,i), TrimLeft */
    size_t ws = strcspn(s, " \t\v");
    if (!s[ws]) { msg |= MP_FS_MSG_NOARG; goto out; }
    s_delete(s, 0, ws);
    s_trim_left(s);
    if (!s[0]) { msg |= MP_FS_MSG_NOARG; goto out; }
    s_trim_right(s);
    if (!s[0]) { msg |= MP_FS_MSG_NOARG; goto out; }
    {   /* ParsePostLongFromDbl(delete=1): ParsePostDouble then (int). ours: [+-]digits[.digits] scanner */
        size_t i = 0;
        if (s[i] == '+' || s[i] == '-') i++;
        size_t d0 = i;
        int dot = 0;
        while ((s[i] >= '0' && s[i] <= '9') || (s[i] == '.' && !dot)) { if (s[i] == '.') dot = 1; i++; }
        int ok = 0;
        for (size_t k = d0; k < i; k++) if (s[k] >= '0' && s[k] <= '9') ok = 1;
        double dv = ok ? strtod(s, NULL) : 0.0;
        short nv = (short)(int)dv;
        f.num = nv;
        if (!(nv > 0 && ok)) { msg |= MP_FS_MSG_BADNUM; goto out; }
        s_delete(s, 0, i);
    }
    s_trim_left(s);
    /* option letters anywhere in the rest */
    unsigned fl = 0;
    if (strchr(s, '+')) fl |= MP_FS_PLUS;
    if (strpbrk(s, "sS")) fl |= MP_FS_SPACE;
    if (strpbrk(s, "lL")) fl |= MP_FS_LEAD;
    if (strpbrk(s, "tT")) fl |= MP_FS_TRAIL;
    if (strpbrk(s, "nN")) fl |= MP_FS_NONMODAL;
    if (!strpbrk(s, "dD")) {
        if (strpbrk(s, "iI")) fl |= MP_FS_INCR;
    } else {
        fl |= MP_FS_DELTA;
        if (strpbrk(s, "iI")) msg |= MP_FS_MSG_ID;               /* 'i' not set */
    }
    if (strpbrk(s, "zZ")) fl |= MP_FS_ZERO;
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);   /* MakeUpper */
    for (const char *o = "+SLTNDIZ"; *o; o++) {                  /* Remove(ch), count > 1 -> 0x8000 */
        int cnt = 0;
        char *w = s;
        for (char *r = s; *r; r++) { if (*r == *o) cnt++; else *w++ = *r; }
        *w = 0;
        if (cnt > 1) msg |= MP_FS_MSG_DUP;
    }
    s_trim_left(s);
    s_trim_right(s);
    f.idig[0] = 0; f.ddig[0] = 4; f.idig[1] = 0; f.ddig[1] = 3; f.round[0] = f.round[1] = 0;
    int v;
    if (!one_digit(s, &v)) { msg |= MP_FS_MSG_BADIINT; goto out; }
    f.idig[0] = v;
    s_delete(s, 0, 1);
    s_trim_left(s);
    if (!s[0]) { msg |= MP_FS_MSG_BADIDEC; goto out; }
    switch (s[0]) {                       /* the inch separator also sets the metric bits (copied if no metric) */
    case '%': fl |= 0x90 | 0x48; break;
    case ',': fl |= 0x48; break;
    case '.': fl |= 0x24; break;
    case '^': fl |= 0x90 | 0x24; break;
    }
    s_trim_left_set(s, ".,^%");
    if (!s[0]) { msg |= MP_FS_MSG_BADIDEC; goto out; }
    if (!one_digit(s, &v)) { f.ddig[0] = 0; msg |= MP_FS_MSG_BADIDEC; goto out; }
    f.ddig[0] = v;
    s_delete(s, 0, 1);
    if (s[0]) {
        if (s[0] >= '0' && s[0] <= '9') {                        /* round digit, must be one of "0259" */
            one_digit(s, &v);
            if (strchr("0259", s[0])) f.round[0] = v;
            else { f.round[0] = 0; msg |= MP_FS_MSG_IRND; }
            s_delete(s, 0, 1);
        }
        s_trim_left(s);
    }
    if (s[0]) {                           /* metric part */
        fl &= 0xff1f;
        if (!one_digit(s, &v)) { f.idig[1] = 0; msg |= MP_FS_MSG_BADMINT; goto out; }
        f.idig[1] = v;
        s_delete(s, 0, 1);
        s_trim_left(s);
        if (!s[0]) { msg |= MP_FS_MSG_BADMDEC; goto out; }
        switch (s[0]) {
        case '%': fl |= 0x80 | 0x40; break;
        case ',': fl |= 0x40; break;
        case '.': fl |= 0x20; break;
        case '^': fl |= 0x80 | 0x20; break;
        }
        s_trim_left_set(s, ".,^%");
        if (!s[0]) { msg |= MP_FS_MSG_BADMDEC; goto out; }
        if (!one_digit(s, &v)) { f.ddig[1] = 0; msg |= MP_FS_MSG_BADMDEC; goto out; }
        f.ddig[1] = v;
        s_delete(s, 0, 1);
        if (s[0]) {
            if (s[0] >= '0' && s[0] <= '9') {
                one_digit(s, &v);
                if (strchr("0259", s[0])) f.round[1] = v;
                else { f.round[1] = 0; msg |= MP_FS_MSG_MRND; }
                s_delete(s, 0, 1);
            }
            s_trim_left(s);
        }
    } else {
        f.idig[1] = f.idig[0];
        f.ddig[1] = f.ddig[0];
        f.round[1] = f.round[0];
    }
    if ((fl & MP_FS_ZERO) && ((fl & 0x10) || (fl & 0x80))) {
        fl &= 0xbfff;
        msg |= MP_FS_MSG_Z;
    }
    if (s[0]) msg |= MP_FS_MSG_TRAIL;
    f.flags = fl;
out:
    if (num) *num = f.num;
    if (msgp) *msgp = msg;
    f.msg = msg;
    if (msg & 1) {
        if (err && errn) snprintf(err, errn, "fs statement error 0x%x", msg);   /* wording ours */
        return 0;
    }
    f.defined = 1;
    mp_fs_set_max(&f);
    if (fs) *fs = f;
    return 1;
}

int mp_fs_parse(const char *line, int *num, MpFs *fs, char *err, size_t errn)
{
    return mp_fs_parse2(line, num, fs, NULL, err, errn);
}
