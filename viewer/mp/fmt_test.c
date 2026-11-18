/* fmt_test - self-test / CLI for mp_fmt (MP number formatting).
 *   fmt_test                         self-tests (tools/mp_format.py cases + decomp-derived cases)
 *   fmt_test fmt "<fs2 line>" V [m] [round_opt]   format V (m = metric)
 *   fmt_test fcvt V NDEC             MSVCR120 _fcvt_s emulation
 *   fmt_test check POST.pst FILE.NC...  oracle: re-format every address word of real Mastercam output with the
 *                                    post's fs2/fmt statements (units per file from G20/G21) and compare the text */
#include "mp_fmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static int fails;

static void t_fmt(const char *spec, double v, int metric, double ro, const char *want)
{
    MpFs fs;
    int num;
    char err[128], got[256];
    if (!mp_fs_parse(spec, &num, &fs, err, sizeof err)) {
        printf("BAD parse %s: %s\n", spec, err);
        fails++;
        return;
    }
    MpFmtCtx cx = {ro};
    mp_fmt_number(&fs, metric, v, &cx, got, sizeof got);
    int ok = strcmp(got, want) == 0;
    if (!ok) fails++;
    printf("%s %-24s %14.9g %-6s ro=%-3g -> '%s' (want '%s')\n", ok ? "ok " : "BAD", spec, v, metric ? "metric" : "inch",
           ro, got, want);
}

static void t_parse(const char *spec, int ok_want, int idi, int ddi, int idm, int ddm, unsigned flags, int ri, int rm,
                    unsigned msg)
{
    MpFs fs;
    int num;
    unsigned m;
    char err[128];
    memset(&fs, 0, sizeof fs);
    int ok = mp_fs_parse2(spec, &num, &fs, &m, err, sizeof err);
    int good = ok == ok_want && m == msg &&
               (!ok || (fs.idig[0] == idi && fs.ddig[0] == ddi && fs.idig[1] == idm && fs.ddig[1] == ddm &&
                        fs.flags == flags && fs.round[0] == ri && fs.round[1] == rm));
    if (!good) fails++;
    printf("%s parse %-26s ok=%d %d.%d %d.%d fl=%#x rnd=%d/%d msg=%#x\n", good ? "ok " : "BAD", spec, ok, fs.idig[0],
           fs.ddig[0], fs.idig[1], fs.ddig[1], fs.flags, fs.round[0], fs.round[1], m);
}

static void t_fcvt(double v, int nd, const char *wbuf, int wdec, int wsign)
{
    char b[400];
    int dec = 0, sign = 0;
    mp_ms_fcvt_s(b, sizeof b, v, nd, &dec, &sign);
    int ok = !strcmp(b, wbuf) && dec == wdec && sign == wsign;
    if (!ok) fails++;
    printf("%s fcvt(%.17g,%d) = '%s' dec=%d sign=%d (want '%s' %d %d)\n", ok ? "ok " : "BAD", v, nd, b, dec, sign, wbuf,
           wdec, wsign);
}

static int selftest(void)
{
    puts("-- tools/mp_format.py self-tests --");
    t_fmt("fs2 2 0.4 0.3", 2.0, 0, 0, "2.");
    t_fmt("fs2 2 0.4 0.3", 0.0, 0, 0, "0.");
    t_fmt("fs2 2 0.4 0.3", -1.08137, 0, 0, "-1.0814");
    t_fmt("fs2 2 0.4 0.3", 1.23456, 1, 0, "1.235");
    t_fmt("fs2 2 0.4 0.3", 0.25, 0, 0, ".25");
    t_fmt("fs2 4 1 0 1 0", 5, 0, 0, "5");
    t_fmt("fs2 5 2 0 2 0l", 7, 0, 0, "07");
    t_fmt("fs2 6 3 0 3 0l", 12, 0, 0, "012");
    t_fmt("fs2 15 0.2 0.1", 6.332032, 0, 0, "6.33");
    t_fmt("fs2 18 2.2 2.2lt", 5.5, 0, 0, "05.50");
    t_fmt("fs2 21 0^7 0^7", 3.0, 0, 0, "3");
    t_fmt("fs2 21 0^7 0^7", 3.5, 0, 0, "3.5");
    t_fmt("fs2 1 0.7 0.6", 0.123456789, 0, 0, ".1234568");

    puts("-- decomp-derived cases (FormattedNumberString @1800bcd70 / FormattedNumber @1800bc9d0) --");
    t_fmt("fs2 2 0.4 0.3", -0.00004, 0, 0, "0.");          /* rounds to empty buffer: sign dropped */
    t_fmt("fs2 2 0.4 0.3", -0.00006, 0, 0, "-.0001");
    t_fmt("fs2 2 0.4 0.3", -0.0004, 1, 0, "0.");
    t_fmt("fs2 2 0.4 0.3", 25.0, 1, 0, "25.");
    t_fmt("fs2 2 0.4 0.3", -0.747, 1, 0, "-.747");
    t_fmt("fs2 15 0.2 0.1", 159.2, 1, 0, "159.2");
    t_fmt("fs2 15 0.2 0.1", 150.0, 1, 0, "150.");
    t_fmt("fs2 21 0^7 0^7", 100, 1, 0, "100");
    t_fmt("fs2 21 0^7 0^7", 0, 1, 0, "0");                 /* "0" appended, '^' and no fraction: no point */
    t_fmt("fs2 4 1 0 1 0", 3183, 1, 0, "3183");
    t_fmt("fs2 4 1 0 1 0", 0, 1, 0, "0");
    t_fmt("fs2 4 1 0 1 0", 2.5, 0, 0, "3");                /* _fcvt(2.5,0) half-up */
    t_fmt("fs2 4 1 0 1 0", -2.5, 0, 0, "-3");
    t_fmt("fs2 7 4 0 4 0l", 0, 0, 0, "0000");
    t_fmt("fs2 7 4 0 4 0l", 1, 0, 0, "0001");
    t_fmt("fs2 19 2 0 2 0t", 3, 0, 0, "3");
    t_fmt("fs2 20 0 2 0 2lt", 0.52, 0, 0, "52");            /* no separator: integer+fraction concatenated */
    t_fmt("fs2 20 0 2 0 2lt", 0.05, 0, 0, "05");
    t_fmt("fs2 30 0 2 0 2t", 0.05, 0, 0, "5");              /* 't' without 'l', empty int: leading zeros dropped */
    t_fmt("fs2 30 0 2 0 2t", 0.0, 0, 0, "0");               /* 't' pads the fraction to "00", then one zero dropped */
    t_fmt("fs2 31 2 2 2 2", 1.5, 0, 0, "15");               /* implied decimal without 't': trailing zeros stripped */
    t_fmt("fs2 32 0.3 0.3z", 2, 0, 0, "2.0");
    t_fmt("fs2 32 0.3 0.3z", 0, 0, 0, "0.0");
    t_fmt("fs2 33 0.3 0.3t", 2, 0, 0, "2.000");
    t_fmt("fs2 33 0.3 0.3t", 0.5, 0, 0, ".500");
    t_fmt("fs2 34 0.3 0.3lt", 0.5, 0, 0, ".500");           /* idig 0: nothing to pad */
    t_fmt("fs2 35 2.3 2.3l", 0.5, 0, 0, "00.5");
    t_fmt("fs2 36 0,3 0,3", 1.25, 0, 0, "1,25");
    t_fmt("fs2 37 0%3 0%3", 2, 0, 0, "2");
    t_fmt("fs2 37 0%3 0%3", 2.5, 0, 0, "2,5");
    t_fmt("fs2 38 0.4 0,3", 2.5, 1, 0, "2,5");
    t_fmt("fs2 38 0.4 0,3", 2.5, 0, 0, "2.5");
    t_fmt("fs2 39 0.3 0.3+", 2.5, 0, 0, "+2.5");
    t_fmt("fs2 39 0.3 0.3+", 0, 0, 0, "+0.");
    t_fmt("fs2 40 0.3 0.3s", 2.5, 0, 0, " 2.5");
    t_fmt("fs2 40 0.3 0.3s", -2.5, 0, 0, "-2.5");
    t_fmt("fs2 41 0.35 0.35", 1.2374, 0, 0, "1.235");       /* round digit 5: steps of .005 */
    t_fmt("fs2 41 0.35 0.35", 1.2324, 0, 0, "1.23");
    t_fmt("fs2 42 0.32 0.32", 1.2374, 0, 0, "1.238");       /* steps of .002 */
    t_fmt("fs2 2 0.4 0.3", 1.00005, 0, 0, "1.0001");        /* 17 digits "100005" -> half-up */
    t_fmt("fs2 10 0.2 0.2", 1.005, 0, 0, "1.");             /* 17 digits "10049999999999999" -> 1.00 */
    t_fmt("fs2 11 0.3 0.3", 0.0115, 0, 0, ".012");          /* MSVC double rounding: exact .01149999.. but 17 digits
                                                               "11500000000000000" -> .012 (correct rounding: .011) */
    t_fmt("fs2 10 0.2 0.2", 2.675, 0, 0, "2.67");
    t_fmt("fs2 10 0.2 0.2", 1.115, 0, 0, "1.12");
    t_fmt("fs2 2 0.4 0.3", 1e21, 0, 0, "100000000000000000000.");   /* ValidateNumber clamps to 1e20 */
    /* round_opt$ tens digit 1/2 = RoundFcvtBuffer digit rounding */
    t_fmt("fs2 11 0.3 0.3", 0.12345, 0, 10, ".124");   /* "45": '4' keeps the tail, 5 cascades -> .124 */
    t_fmt("fs2 11 0.3 0.3", 0.12345, 0, 0, ".123");
    t_fmt("fs2 11 0.3 0.3", 0.12345, 0, 20, ".123");        /* mode 2: tail digits carry only if >= '9' */
    t_fmt("fs2 11 0.3 0.3", 0.12349, 0, 20, ".124");
    t_fmt("fs2 11 0.3 0.3", 0.9996, 0, 10, "1.");

    puts("-- MSVCR120 _fcvt_s emulation --");
    t_fcvt(3.1415926535, 7, "31415927", 1, 0);
    t_fcvt(0.0006, 3, "1", -2, 0);
    t_fcvt(0.0004, 3, "", -3, 0);
    t_fcvt(0.00001, 3, "", -4, 0);
    t_fcvt(-2.5, 0, "3", 1, 1);
    t_fcvt(0.0, 3, "000", 0, 0);
    t_fcvt(9.9996, 3, "10000", 2, 0);
    t_fcvt(1.005, 2, "100", 1, 0);
    t_fcvt(0.0115, 3, "12", -1, 0);

    puts("-- ParseFs2 @180098130 --");
    t_parse("fs2 2 0.4 0.3", 1, 0, 4, 0, 3, 0x24, 0, 0, 0);
    t_parse("fs2 3 0.4 0.3d", 1, 0, 4, 0, 3, 0x2024, 0, 0, 0);
    t_parse("fs2 4 1 0 1 0", 1, 1, 0, 1, 0, 0, 0, 0, 0);
    t_parse("fs2 18 2.2 2.2lt", 1, 2, 2, 2, 2, 0x324, 0, 0, 0);
    t_parse("fs2 21 0^7 0^7", 1, 0, 7, 0, 7, 0xb4, 0, 0, 0);
    t_parse("fs 1 0.4", 1, 0, 4, 0, 4, 0x24, 0, 0, 0);        /* no metric part: copied from inch */
    t_parse("fs2 5 0.45 0.35   # comment", 1, 0, 4, 0, 3, 0x24, 5, 5, 0);
    t_parse("fs2 6 0.47 0.3", 1, 0, 4, 0, 3, 0x24, 0, 0, 0x202);
    t_parse("fs2 7 0.4 0.3di", 1, 0, 4, 0, 3, 0x2024, 0, 0, 0x2002);
    t_parse("fs2 8 0^4 0^3z", 1, 0, 4, 0, 3, 0xb4, 0, 0, 0x4002);
    t_parse("fs2 9 0.4 0.3ll", 1, 0, 4, 0, 3, 0x124, 0, 0, 0x8000);
    t_parse("fs2 10 0.4 0,3", 1, 0, 4, 0, 3, 0x44, 0, 0, 0);
    t_parse("fs2 11 0.4 0.3 x", 1, 0, 4, 0, 3, 0x24, 0, 0, 0x10000);
    t_parse("fs2 0 0.4 0.3", 0, 0, 0, 0, 0, 0, 0, 0, 0x41);
    t_parse("fs2", 0, 0, 0, 0, 0, 0, 0, 0, 0x21);
    t_parse("fs2 12 1", 0, 0, 0, 0, 0, 0, 0, 0, 0x101);
    t_parse("fs2 13 0.4 1", 0, 0, 0, 0, 0, 0, 0, 0, 0x801);
    t_parse("fs2 14 x", 0, 0, 0, 0, 0, 0, 0, 0, 0x81);
    {
        MpFs fs;
        int num;
        mp_fs_parse("fs2 2 2.4 2.3", &num, &fs, NULL, 0);
        int ok = fabs(fs.maxv[0] - 99.9999) < 1e-12 && fabs(fs.maxv[1] - 99.999) < 1e-12;
        MpFs g;
        mp_fs_parse("fs2 2 0.4 3.35", &num, &g, NULL, 0);
        ok &= g.maxv[0] == 1e20 && fabs(g.maxv[1] - 999.995) < 1e-12;
        if (!ok) fails++;
        printf("%s SetMaxFormatNumber %.10g %.10g %.10g %.10g\n", ok ? "ok " : "BAD", fs.maxv[0], fs.maxv[1], g.maxv[0],
               g.maxv[1]);
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "all ok", fails);
    return fails ? 1 : 0;
}

/* ---- oracle check against real NC output ---- */
typedef struct {
    char letter;
    int fs[16];
    int nfs;
} Addr;

static MpFs g_fs[256];
static Addr g_addr[26];

static int load_post(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    char line[2048];
    int nfs = 0, nfmt = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if ((!strncmp(p, "fs2 ", 4) || !strncmp(p, "fs ", 3) || !strncmp(p, "fs2\t", 4))) {
            MpFs fs;
            int num;
            char err[64];
            if (mp_fs_parse(p, &num, &fs, err, sizeof err) && num > 0 && num < 256) { g_fs[num] = fs; nfs++; }
        } else if (!strncmp(p, "fmt", 3) && (p[3] == ' ' || p[3] == '\t')) {
            p += 3;
            while (*p == ' ' || *p == '\t') p++;
            if (p[0] == '"' && isupper((unsigned char)p[1]) && p[2] == '"') {
                char L = p[1];
                int num = atoi(p + 3);
                Addr *a = &g_addr[L - 'A'];
                a->letter = L;
                int dup = 0;
                for (int i = 0; i < a->nfs; i++) dup |= a->fs[i] == num;
                if (!dup && a->nfs < 16) a->fs[a->nfs++] = num;
                nfmt++;
            }
        }
    }
    fclose(f);
    printf("post %s: %d fs statements, %d single-letter fmt\n", path, nfs, nfmt);
    for (int i = 0; i < 26; i++)
        if (g_addr[i].nfs) {
            printf("  %c:", 'A' + i);
            for (int k = 0; k < g_addr[i].nfs; k++) printf(" %d", g_addr[i].fs[k]);
            printf("\n");
        }
    return 1;
}

static long n_words, n_ok, n_nofmt, n_bad;

static void check_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return; }
    static char text[1 << 22];
    size_t L = fread(text, 1, sizeof text - 1, f);
    text[L] = 0;
    fclose(f);
    int metric = strstr(text, "G21") != NULL;
    long w = 0, ok = 0, bad = 0, nofmt = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        if (strchr(line, '#') || strchr(line, '[') || strchr(line, '=')) continue;   /* macro lines (hand-written) */
        char clean[2048];
        size_t c = 0;
        int depth = 0;
        for (char *p = line; *p && c < sizeof clean - 1; p++) {   /* drop (comments) */
            if (*p == '(') depth++;
            else if (*p == ')') { if (depth) depth--; }
            else if (!depth) clean[c++] = *p;
        }
        clean[c] = 0;
        for (char *p = clean; *p;) {
            if (isupper((unsigned char)*p) && (p == clean || !isalnum((unsigned char)p[-1])) &&
                (p[1] == '-' || p[1] == '+' || p[1] == '.' || isdigit((unsigned char)p[1]))) {
                char L = *p;
                char *q = p + 1;
                if (*q == '-' || *q == '+') q++;
                while (isdigit((unsigned char)*q) || *q == '.') q++;
                if (isalpha((unsigned char)*q)) { p = q; continue; }   /* e.g. part of a word */
                char tok[64];
                size_t tl = (size_t)(q - p - 1);
                if (tl >= sizeof tok) tl = sizeof tok - 1;
                memcpy(tok, p + 1, tl);
                tok[tl] = 0;
                double v = strtod(tok, NULL);
                w++;
                Addr *a = &g_addr[L - 'A'];
                if (!a->nfs) { nofmt++; p = q; continue; }
                int hit = 0;
                char got[128], first[128] = "";
                for (int k = 0; k < a->nfs && !hit; k++) {
                    MpFs *fs = &g_fs[a->fs[k]];
                    if (!fs->defined) continue;
                    mp_fmt_number(fs, metric, v, NULL, got, sizeof got);
                    if (!first[0]) snprintf(first, sizeof first, "%s", got);
                    hit = !strcmp(got, tok);
                }
                if (hit) ok++;
                else {
                    bad++;
                    if (bad <= 8) printf("    MISMATCH %s: %c%s -> %c%s\n", path, L, tok, L, first);
                }
                p = q;
            } else
                p++;
        }
    }
    printf("  %-70.70s %s words=%ld ok=%ld bad=%ld no-fmt=%ld\n", path, metric ? "mm" : "in", w, ok, bad, nofmt);
    n_words += w; n_ok += ok; n_bad += bad; n_nofmt += nofmt;
}

int main(int argc, char **argv)
{
    if (argc < 2) return selftest();
    if (!strcmp(argv[1], "fmt") && argc >= 4) {
        MpFs fs;
        int num;
        char err[128], out[256];
        unsigned msg;
        if (!mp_fs_parse2(argv[2], &num, &fs, &msg, err, sizeof err)) { printf("error: %s\n", err); return 1; }
        MpFmtCtx cx = {argc > 5 ? atof(argv[5]) : 0.0};
        int metric = argc > 4 && argv[4][0] == 'm';
        double r = 0;
        mp_fmt_number_ex(&fs, metric, atof(argv[3]), &cx, MP_FMT_ROUND, out, sizeof out, &r, NULL);
        printf("fs %d: int %d.%d / %d.%d flags %#x round %d/%d msg %#x max %.10g/%.10g\n", num, fs.idig[0], fs.ddig[0],
               fs.idig[1], fs.ddig[1], fs.flags, fs.round[0], fs.round[1], msg, fs.maxv[0], fs.maxv[1]);
        printf("'%s'  (rounded value %.17g)\n", out, r);
        return 0;
    }
    if (!strcmp(argv[1], "fcvt") && argc >= 4) {
        char b[400];
        int dec = 0, sign = 0;
        int e = mp_ms_fcvt_s(b, sizeof b, atof(argv[2]), atoi(argv[3]), &dec, &sign);
        printf("rc=%d '%s' decpt=%d sign=%d\n", e, b, dec, sign);
        return 0;
    }
    if (!strcmp(argv[1], "batch")) {   /* stdin: "<fs line>\t<value>\t<i|m>" -> formatted text per line */
        char line[1024];
        while (fgets(line, sizeof line, stdin)) {
            char *t1 = strchr(line, '\t');
            if (!t1) continue;
            *t1++ = 0;
            char *t2 = strchr(t1, '\t');
            int metric = t2 && t2[1] == 'm';
            MpFs fs;
            int num;
            char out[256];
            if (!mp_fs_parse(line, &num, &fs, NULL, 0)) { puts("#ERR"); continue; }
            mp_fmt_number(&fs, metric, strtod(t1, NULL), NULL, out, sizeof out);
            printf("%s\n", out);
        }
        return 0;
    }
    if (!strcmp(argv[1], "check") && argc >= 4) {
        if (!load_post(argv[2])) return 1;
        for (int i = 3; i < argc; i++) check_file(argv[i]);
        printf("TOTAL words=%ld ok=%ld bad=%ld no-fmt(letter without fmt in post)=%ld\n", n_words, n_ok, n_bad, n_nofmt);
        return n_bad ? 1 : 0;
    }
    fprintf(stderr, "usage: fmt_test | fmt_test fmt \"fs2 N a.b c.d\" V [m] [round_opt] | fmt_test fcvt V NDEC |"
                    " fmt_test check POST.pst FILE.NC...\n");
    return 2;
}
