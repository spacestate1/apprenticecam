/* cui_nest_ini.inc - included by cui_nest.c: NESTING.DLL's settings files - the CParam block, the [PARAMETERS] / [SHEETn] /
 * [PARTn] / [GROUPn] readers and writers, the defaults file (.defx), sessions (.nstx), the data folder.
 * Citations as in cui_nest.c ("L<n>" = decomp/nest/nesting_all.c). formats/nesting_addin.md §3 / §12.
 *
 * Windows' profile API is emulated on the file's lines (NsIni): GetPrivateProfileStringA (section / key matched without case,
 * the value trimmed, enclosing quotes dropped, at most 255 characters) and WritePrivateProfileStringA (an existing key's line
 * replaced, else the line added after the section's last line, a missing section appended; CR LF lines). Ours: paths are
 * this platform's - add_prefix_suffix treats '/' like '\\', and the upper-cased file name of the status prompt
 * (FUN_180039080's _mbsupr on the caller's buffer) is not used to open the file (a case-sensitive file system). */

/* ------------------------------------------------------------------------------------------------ the data folder */
/* FUN_180043c20 L47031: "<app data>\Nesting\" (made when missing; 10012 when it cannot be made); IMG\ / XML\ are the report
   folders. Ours (user decision NS-1): paths.conf key `nesting` (MCAMUI_NESTING), default <config dir>/Nesting/, IMG/ and XML/
   made in it; never the Windows disc */
static char ns_dir[1100];
static const char *ns_data_dir(void) {
  if (ns_dir[0]) return ns_dir;
  const char *k = site_path("nesting"); char d[1100];
  if (k && k[0]) snprintf(d, sizeof d, "%s", k); else config_file(d, sizeof d, "Nesting");
  size_t l = strlen(d); if (l && !is_sep(d[l - 1])) snprintf(d + l, sizeof d - l, "/");
  make_dirs(d);
  char sub[1200]; snprintf(sub, sizeof sub, "%sIMG", d); make_dirs(sub); snprintf(sub, sizeof sub, "%sXML", d); make_dirs(sub);
  snprintf(ns_dir, sizeof ns_dir, "%s", d);
  return ns_dir;
}
/* mccore add_prefix_suffix_s @1806f1ca0 (asm): no ':' and no '\\' in NAME -> PREFIX + NAME; no '.' in the original NAME -> NAME +
   SUFFIX. (add_prefix_suffix, the unsized export, is the same with 0x100) */
static void ns_add_prefix_suffix(char *name, size_t n, const char *prefix, const char *suffix) {
  char orig[1100]; snprintf(orig, sizeof orig, "%s", name);
  if (!strchr(orig, ':') && !strchr(orig, '\\') && !strchr(orig, '/')) { char t[1200]; snprintf(t, sizeof t, "%s%s", prefix, orig); snprintf(name, n, "%s", t); }
  if (!strchr(orig, '.')) { size_t l = strlen(name); snprintf(name + l, n - l, "%s", suffix); }
}
static int ns_file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return 0; fclose(f); return 1; }
static void ns_upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }

/* the status prompts: FUN_180039080 L39161 "Writing" / "Reading" (10057 / 10058) + " " + FILE (upper case) + " " + "..." (10059);
   FUN_180039230 L39234: FILE + "\n" (ours: a space, the prompt is one line) + 10131 "File written successfully." / 10132 "File read successfully."; on failure 10074
   "Warning - cannot access file." with FILE (a message box when asked, else the prompt) */
static void ns_prompt_begin(const char *file, int writing) {
  char u[1100]; snprintf(u, sizeof u, "%s", file); ns_upper(u);
  snprintf(cam_msg, sizeof cam_msg, "%s %s %s", writing ? "Writing" : "Reading", u, "...");
}
static void ns_prompt_end(const char *file, int writing, int ok, int box) {
  char u[1100]; snprintf(u, sizeof u, "%s", file); ns_upper(u);
  if (!ok) { cam_msg[0] = 0; if (box) ns_msg("Warning - cannot access file.\n%s", u); return; }
  snprintf(cam_msg, sizeof cam_msg, "%s %s", u, writing ? "File written successfully." : "File read successfully.");
}

/* ------------------------------------------------------------------------------------------------ the profile emulation */
typedef struct { char **l; int n, cap; } NsIni;
static void ini_free(NsIni *f) { for (int i = 0; i < f->n; i++) free(f->l[i]); free(f->l); memset(f, 0, sizeof *f); }
static void ini_insert(NsIni *f, int at, const char *s) {
  if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 64; f->l = realloc(f->l, sizeof(char *) * (size_t)f->cap); }
  memmove(f->l + at + 1, f->l + at, sizeof(char *) * (size_t)(f->n - at)); f->l[at] = strdup(s); f->n++;
}
static void ini_text(NsIni *f, const char *t) {
  memset(f, 0, sizeof *f);
  while (*t) { const char *e = strchr(t, '\n'); size_t k = e ? (size_t)(e - t) : strlen(t); char b[2048];
    if (k > sizeof b - 1) k = sizeof b - 1; memcpy(b, t, k); b[k] = 0; if (k && b[k - 1] == '\r') b[k - 1] = 0;
    ini_insert(f, f->n, b); if (!e) break; t = e + 1; }
}
static int ini_load(NsIni *f, const char *path) {
  memset(f, 0, sizeof *f); FILE *fp = fopen(path, "rb"); if (!fp) return 0;
  fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
  char *b = malloc((size_t)sz + 1); size_t r = fread(b, 1, (size_t)sz, fp); b[r] = 0; fclose(fp);
  ini_text(f, b); free(b); return 1;
}
static int ini_save(const NsIni *f, const char *path) {
  FILE *fp = fopen(path, "wb"); if (!fp) return 0;
  for (int i = 0; i < f->n; i++) fprintf(fp, "%s\r\n", f->l[i]);
  return fclose(fp) == 0;
}
static const char *ini_trim(const char *s, size_t *len) {   /* leading / trailing blanks */
  while (*s == ' ' || *s == '\t') s++;
  size_t k = strlen(s); while (k && (s[k - 1] == ' ' || s[k - 1] == '\t')) k--;
  *len = k; return s;
}
static int ini_is_sec(const char *line, const char *sec) {
  size_t k; const char *s = ini_trim(line, &k);
  if (k < 2 || s[0] != '[') return 0;
  const char *e = memchr(s, ']', k); if (!e) return 0;
  size_t n2; const char *in = s + 1; char b[512]; size_t m = (size_t)(e - in); if (m >= sizeof b) m = sizeof b - 1; memcpy(b, in, m); b[m] = 0;
  in = ini_trim(b, &n2); return n2 == strlen(sec) && !strncasecmp(in, sec, n2);
}
static int ini_sec(const NsIni *f, const char *sec) { for (int i = 0; i < f->n; i++) if (ini_is_sec(f->l[i], sec)) return i; return -1; }
static int ini_sec_end(const NsIni *f, int h) {
  for (int i = h + 1; i < f->n; i++) { size_t k; const char *s = ini_trim(f->l[i], &k); if (k && s[0] == '[') return i; }
  return f->n;
}
static int ini_key_line(const NsIni *f, int h, const char *key) {   /* the key's line in section H, -1 if none */
  int e = ini_sec_end(f, h);
  for (int i = h + 1; i < e; i++) {
    const char *eq = strchr(f->l[i], '='); if (!eq) continue;
    char b[512]; size_t m = (size_t)(eq - f->l[i]); if (m >= sizeof b) m = sizeof b - 1; memcpy(b, f->l[i], m); b[m] = 0;
    size_t k; const char *s = ini_trim(b, &k);
    if (k == strlen(key) && !strncasecmp(s, key, k)) return i;
  }
  return -1;
}
/* GetPrivateProfileStringA(sec, key, "", out, n): the value's length (0: missing or empty) */
static int GPPS(const NsIni *f, const char *sec, const char *key, char *out, int n) {
  out[0] = 0; int h = ini_sec(f, sec); if (h < 0) return 0;
  int i = ini_key_line(f, h, key); if (i < 0) return 0;
  size_t k; const char *v = ini_trim(strchr(f->l[i], '=') + 1, &k);
  if (k >= 2 && (v[0] == '"' || v[0] == '\'') && v[k - 1] == v[0]) { v++; k -= 2; }
  if ((int)k > n - 1) k = (size_t)(n - 1);
  memcpy(out, v, k); out[k] = 0; return (int)k;
}
/* WritePrivateProfileStringA(sec, key, val) */
static void WPPS(NsIni *f, const char *sec, const char *key, const char *val) {
  char line[2200]; snprintf(line, sizeof line, "%s=%s", key, val);
  int h = ini_sec(f, sec);
  if (h < 0) { char hd[600]; snprintf(hd, sizeof hd, "[%s]", sec); ini_insert(f, f->n, hd); ini_insert(f, f->n, line); return; }
  int i = ini_key_line(f, h, key);
  if (i >= 0) { free(f->l[i]); f->l[i] = strdup(line); return; }
  int e = ini_sec_end(f, h); while (e > h + 1 && !f->l[e - 1][0]) e--;   /* after the section's last non-empty line */
  ini_insert(f, e, line);
}

/* ---- typed writers (section / key upper-cased; typed values upper-cased) */
/* FUN_1800c9ed0(v, 5) L139205 (asm @1800c9f34): |v| < tol "0.0"; >= 1e38 "**********"; "%.*f" with n_places_an (config 29003)
   + 5 places; trailing '0's removed while the character before is not '.' */
static void ns_dbl_str(double v, char *b, size_t n) {
  if (!(mc_systol.tol <= fabs(v))) { snprintf(b, n, "0.0"); return; }
  if (fabs(v) >= 1e38) { snprintf(b, n, "**********"); return; }
  snprintf(b, n, "%.*f", (cfg_places >= 0 ? cfg_places : 4) + 5, v);
  int k = (int)strlen(b); while (k > 1 && b[k - 1] == '0' && b[k - 2] != '.') b[--k] = 0;
}
static void wput(NsIni *f, const char *sec, const char *key, const char *val, int up) {
  char s[300], k[300], v[1100]; snprintf(s, sizeof s, "%s", sec); snprintf(k, sizeof k, "%s", key); snprintf(v, sizeof v, "%s", val);
  ns_upper(s); ns_upper(k); if (up) ns_upper(v); WPPS(f, s, k, v);
}
static void w_dbl(NsIni *f, const char *sec, const char *key, double v) { char b[64]; ns_dbl_str(v, b, sizeof b); wput(f, sec, key, b, 1); }   /* FUN_1800d5bc0 */
static void w_yes(NsIni *f, const char *sec, const char *key, int c) { wput(f, sec, key, c ? "Yes" : "No", 1); }                   /* FUN_1800d3f00 (char) */
static void w_int(NsIni *f, const char *sec, const char *key, int v) { char b[32]; snprintf(b, sizeof b, "%d", v); wput(f, sec, key, b, 1); }   /* FUN_1800d4ea0 */
static void w_ybool(NsIni *f, const char *sec, const char *key, int v) {   /* FUN_1800d4fc0: "%d", then FUN_1800d41c0: 0 -> NO, 1 -> YES */
  char b[32]; snprintf(b, sizeof b, "%d", v);
  if (!strcmp(b, "0")) snprintf(b, sizeof b, "NO"); else if (!strcmp(b, "1")) snprintf(b, sizeof b, "YES");
  wput(f, sec, key, b, 1);
}
static void w_short(NsIni *f, const char *sec, const char *key, int v) { char b[32]; snprintf(b, sizeof b, "%hd", (short)v); wput(f, sec, key, b, 1); }   /* FUN_1800d5ce0 */
static void w_ulong(NsIni *f, const char *sec, const char *key, unsigned v) { char b[32]; snprintf(b, sizeof b, "%u", v); wput(f, sec, key, b, 1); }   /* FUN_1800d5f10 "%lu" */
static void w_chr(NsIni *f, const char *sec, const char *key, char c) { char b[4]; if (c == ' ') snprintf(b, sizeof b, "_"); else snprintf(b, sizeof b, "%c", c); wput(f, sec, key, b, 1); }   /* FUN_1800d4060 */
static void w_str(NsIni *f, const char *sec, const char *key, const char *v) { wput(f, sec, key, v, 0); }   /* FUN_1800d5e00 / FUN_1800d4290 */
static void w_pt(NsIni *f, const char *sec, const char *key, double x, double y) {   /* FUN_1800d57d0: KEY-X / KEY-Y */
  char k[300]; snprintf(k, sizeof k, "%s-X", key); w_dbl(f, sec, k, x); snprintf(k, sizeof k, "%s-Y", key); w_dbl(f, sec, k, y);
}
static void w_pt3(NsIni *f, const char *sec, const char *key, const double *p) {   /* FUN_1800d5980 */
  char k[300]; snprintf(k, sizeof k, "%s-X", key); w_dbl(f, sec, k, p[0]); snprintf(k, sizeof k, "%s-Y", key); w_dbl(f, sec, k, p[1]);
  snprintf(k, sizeof k, "%s-Z", key); w_dbl(f, sec, k, p[2]);
}

/* ---- typed readers: 1 = found (the value set) */
static void ns_yesno(char *b) {   /* FUN_1800d12c0 L144332 */
  if (!strcasecmp(b, "False") || !strcasecmp(b, "No") || !strcasecmp(b, "N")) snprintf(b, 2, "0");
  if (!strcasecmp(b, "True") || !strcasecmp(b, "Yes") || !strcasecmp(b, "Y")) snprintf(b, 2, "1");
}
static int r_int(const NsIni *f, const char *sec, const char *key, int *v) {   /* FUN_1800d2360 / FUN_1800d3500 */
  char b[256]; if (!GPPS(f, sec, key, b, sizeof b)) return 0; ns_yesno(b); *v = atoi(b); return 1;
}
static int r_short(const NsIni *f, const char *sec, const char *key, short *v) { int k; if (!r_int(f, sec, key, &k)) return 0; *v = (short)k; return 1; }   /* FUN_1800d3200 */
static int r_byte(const NsIni *f, const char *sec, const char *key, uint8_t *v) { int k; if (!r_int(f, sec, key, &k)) return 0; *v = (uint8_t)k; return 1; }   /* FUN_1800d0f80 */
static int r_bool(const NsIni *f, const char *sec, const char *key, uint8_t *v) {   /* FUN_1800d0da0: only "0" / "1" after FUN_1800d12c0 */
  char b[256]; if (!GPPS(f, sec, key, b, sizeof b)) return 0; ns_yesno(b);
  if (!strcmp(b, "0")) { *v = 0; return 1; } if (!strcmp(b, "1")) { *v = 1; return 1; } return 0;
}
static int r_dbl(const NsIni *f, const char *sec, const char *key, double *v) {   /* FUN_1800d3060: atof, |v| < tol -> 0 */
  char b[256]; if (!GPPS(f, sec, key, b, sizeof b)) return 0; *v = atof(b); if (fabs(*v) < mc_systol.tol) *v = 0; return 1;
}
static int r_chr(const NsIni *f, const char *sec, const char *key, char *v) {   /* FUN_1800d1100: '_' = ' ', else the first non-blank */
  char b[256]; int k = GPPS(f, sec, key, b, sizeof b);
  for (int i = 0; i < k; i++) { if (b[i] == '_') { *v = ' '; return 1; } if ((b[i] & 0xdf) != 0) { *v = b[i]; return 1; } }
  return 0;
}
static int r_str(const NsIni *f, const char *sec, const char *key, char *v, int n) {   /* FUN_1800d3380 (FUN_1800d1360: 1000) */
  char b[1100]; if (!GPPS(f, sec, key, b, sizeof b)) return 0; snprintf(v, (size_t)n, "%s", b); return 1;
}
static int r_pt(const NsIni *f, const char *sec, const char *key, double *x, double *y) {   /* FUN_1800d2c80: KEY-X then KEY-Y */
  char k[300]; snprintf(k, sizeof k, "%s-X", key); if (!r_dbl(f, sec, k, x)) return 0;
  snprintf(k, sizeof k, "%s-Y", key); return r_dbl(f, sec, k, y);
}
static int r_pt3(const NsIni *f, const char *sec, const char *key, double *p) {   /* FUN_1800d2e20 */
  char k[300]; snprintf(k, sizeof k, "%s-X", key); if (!r_dbl(f, sec, k, p)) return 0;
  snprintf(k, sizeof k, "%s-Y", key); if (!r_dbl(f, sec, k, p + 1)) return 0;
  snprintf(k, sizeof k, "%s-Z", key); return r_dbl(f, sec, k, p + 2);
}

/* ------------------------------------------------------------------------------------------------ the CParam block */
#define CPB(p, o) ((p)->cp[o])
#define CPI(p, o) (*(int32_t *)((p)->cp + (o)))
#define CPS(p, o) (*(int16_t *)((p)->cp + (o)))
#define CPD(p, o) (*(double *)((p)->cp + (o)))
/* the named fields <-> the block (their offsets: §3.1) */
static void ns_pull(NsParams *p) {
  p->trueshape = CPB(p, 0x1f); p->resolution = CPD(p, 0x20); p->rescbox = CPI(p, 0x28); p->resuser = CPD(p, 0x30);
  p->s2s = CPD(p, 0x38); p->s2p = CPD(p, 0x40); p->p2p = CPD(p, 0x48); p->fitpip = CPI(p, 0x50); p->guillotine = CPI(p, 0x54);
  p->fillall = CPI(p, 0x58); p->autopairs = CPI(p, 0x5c); p->fillers = CPI(p, 0x60); p->prefhole = CPI(p, 0x64);
  p->deletechains = CPI(p, 0x68); p->cyclecolors = CPI(p, 0x74); p->cyclelevels = CPI(p, 0x78); p->cycleunused = CPI(p, 0x7c);
  p->resultcolor = CPI(p, 0x80); p->resultlevel = CPI(p, 0x84); p->autoattach = CPI(p, 0x8c); p->dispgroups = CPI(p, 0x90);
  p->dispresults = CPI(p, 0x94); p->loaddefsheet = CPI(p, 0x98); p->highres = CPI(p, 0x9c); p->savescrap = CPI(p, 0xa0);
  p->addasgroup = CPI(p, 0xa4); p->alignlong = CPI(p, 0xa8); p->ignoreholes = CPI(p, 0xac); p->corner = CPI(p, 0xb0);
  p->scannotes = CPI(p, 0xbc); p->addlabels = CPI(p, 0xc0); p->labelh = CPD(p, 0xc8); p->labelauto = CPB(p, 0xd0);
  p->autoorigins = CPI(p, 0xd4); p->grain = (char)CPB(p, 0xd8); p->filldir = (char)CPB(p, 0xd9); p->nameaslabel = CPB(p, 0xa);
  p->checkdup = CPB(p, 0xb); p->compress = CPB(p, 0x1e); p->ptol = CPD(p, 0x148);
}
static void ns_push(NsParams *p) {
  CPB(p, 0x1f) = (uint8_t)p->trueshape; CPD(p, 0x20) = p->resolution; CPI(p, 0x28) = p->rescbox; CPD(p, 0x30) = p->resuser;
  CPD(p, 0x38) = p->s2s; CPD(p, 0x40) = p->s2p; CPD(p, 0x48) = p->p2p; CPI(p, 0x50) = p->fitpip; CPI(p, 0x54) = p->guillotine;
  CPI(p, 0x58) = p->fillall; CPI(p, 0x5c) = p->autopairs; CPI(p, 0x60) = p->fillers; CPI(p, 0x64) = p->prefhole;
  CPI(p, 0x68) = p->deletechains; CPI(p, 0x74) = p->cyclecolors; CPI(p, 0x78) = p->cyclelevels; CPI(p, 0x7c) = p->cycleunused;
  CPI(p, 0x80) = p->resultcolor; CPI(p, 0x84) = p->resultlevel; CPI(p, 0x8c) = p->autoattach; CPI(p, 0x90) = p->dispgroups;
  CPI(p, 0x94) = p->dispresults; CPI(p, 0x98) = p->loaddefsheet; CPI(p, 0x9c) = p->highres; CPI(p, 0xa0) = p->savescrap;
  CPI(p, 0xa4) = p->addasgroup; CPI(p, 0xa8) = p->alignlong; CPI(p, 0xac) = p->ignoreholes; CPI(p, 0xb0) = p->corner;
  CPI(p, 0xbc) = p->scannotes; CPI(p, 0xc0) = p->addlabels; CPD(p, 0xc8) = p->labelh; CPB(p, 0xd0) = (uint8_t)p->labelauto;
  CPI(p, 0xd4) = p->autoorigins; CPB(p, 0xd8) = (uint8_t)p->grain; CPB(p, 0xd9) = (uint8_t)p->filldir; CPB(p, 0xa) = (uint8_t)p->nameaslabel;
  CPB(p, 0xb) = (uint8_t)p->checkdup; CPB(p, 0x1e) = (uint8_t)p->compress; CPD(p, 0x148) = p->ptol;
}
/* FUN_18004e990 L55000 / FUN_18004e910 L54989: the duplicate-tool-check switches from the mode (+0x1b4) */
static void cp_duptool_mode(NsParams *p, int mode) {
  CPS(p, 0x1b4) = (int16_t)mode;
  if (mode == 0) { memset(p->cp + 0x1b4, 1, 8); CPI(p, 0x1bc) = 0x01010101; CPS(p, 0x1c0) = 0x101; CPB(p, 0x1b6) = 0; CPS(p, 0x1bd) = 0; CPS(p, 0x1b4) = 0; }
  else if (mode == 1) { memset(p->cp + 0x1b4, 1, 8); CPI(p, 0x1bc) = 0x01010101; CPS(p, 0x1c0) = 0x101; CPS(p, 0x1b4) = 1; }
  else if (mode == 2) { memset(p->cp + 0x1b4, 0, 8); CPI(p, 0x1bc) = 0; CPS(p, 0x1c0) = 0; CPS(p, 0x1b4) = 2; }
}
/* FUN_18004e820 L54918 (via FUN_18004e900): the mode from the twelve switches 0x1b6..0x1c1 */
static void cp_duptool_from_flags(NsParams *p) {
  const uint8_t *b = p->cp + 0x1b6; int z[12]; for (int i = 0; i < 12; i++) z[i] = b[i] == 0;
  int mode;
  if (z[11]) { int all = 1; for (int i = 0; i < 11; i++) all &= z[i]; mode = all ? 2 : 3; }
  else if (!z[10] && !z[9] && z[8] && z[7] && !z[6] && !z[5] && !z[4] && !z[3] && !z[2] && !z[1] && z[0]) mode = 0;
  else { int none = 1; for (int i = 0; i < 11; i++) none &= !z[i]; mode = none ? 1 : 3; }
  CPS(p, 0x1b4) = (int16_t)mode;
}
/* the CParam ctor FUN_18004e070 L54639 (+ COnion FUN_18004e000 L54616 at +0xe0, FUN_18004e910(.., 0)); +0x1f = the licence
   result DAT_18013529c - the licence check is dropped (user decision): 1, a licensed Mastercam's value */
static void params_ctor(NsParams *p) {
  memset(p, 0, sizeof *p);
  CPB(p, 0x1f) = 1; CPI(p, 0x18) = 1; CPB(p, 0xd8) = 'N'; CPB(p, 0xd9) = ' '; CPI(p, 0x94) = 1; CPD(p, 0x20) = 1.0; CPD(p, 0x30) = 1.0; CPD(p, 0x38) = 1.0;
  CPD(p, 0x48) = 0.1; CPI(p, 0x5c) = 1; CPI(p, 0x60) = 1; CPI(p, 0x80) = 10; CPI(p, 0x84) = 1; CPI(p, 0xa8) = 1; CPI(p, 0xb0) = 1;
  CPI(p, 0xb4) = 1; CPD(p, 0xc8) = 0.25; CPB(p, 0xd0) = 1; CPI(p, 0xd4) = 1; CPD(p, 0x138) = 14.0;
  CPI(p, 0x154) = 2; CPB(p, 0x166) = 1;   /* +0x164 = 0x10000 */
  CPD(p, 0x148) = ns_metric ? 0.1 : 0.01;
  CPS(p, 0x170) = 1; CPD(p, 0x188) = 1.0;
  cp_duptool_mode(p, 0);
  CPB(p, 0x152) = 1;
  CPB(p, 0xe9) = 1; CPB(p, 0xea) = 1; CPD(p, 0xf0) = 5.0; CPD(p, 0xf8) = 5.0; CPD(p, 0x110) = 0.025; CPB(p, 0x100) = 1;   /* COnion */
  CPD(p, 0x118) = 0.375; CPD(p, 0x128) = 2.0; CPB(p, 0x108) = 1;
  snprintf(p->scrapname, sizeof p->scrapname, "scrap");
  ns_pull(p);
}
/* FUN_18004e650 L54859 + FUN_18004e3e0 L54798: the CParam copy (not every field: +0xd8, the common grain, stays) */
static void params_copy(NsParams *d, NsParams *s) {
  ns_push(s); ns_push(d);
  static const uint16_t b1[] = { 0x1f, 0x08, 0x09, 0x1c, 0x1d, 0x1e, 0xd9, 0x0a, 0x0b, 0xd0, 0x152, 0xe8, 0xe9, 0xea, 0x100, 0x108, 0x120, 0x0c, 0x0d,
    0x16e, 0x16f, 0x170, 0x171, 0x172, 0x1a8, 0x1a9, 0x1b0, 0x1b1 };
  static const uint16_t b4[] = { 0x54, 0x88, 0x8c, 0x94, 0x90, 0x98, 0x28, 0x50, 0x58, 0x5c, 0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x78, 0x7c,
    0x80, 0x84, 0xa0, 0xa4, 0xa8, 0xac, 0x9c, 0xb0, 0xb4, 0xc0, 0xb8, 0xbc, 0xd4, 0x154, 0x158, 0x15c, 0x160, 0x1ac, 0x1bc, 0x104, 0x18 };
  static const uint16_t b8[] = { 0x20, 0x30, 0x38, 0x40, 0x48, 0xc8, 0x138, 0x148, 0x178, 0x180, 0x188, 0x190, 0x198, 0x1a0, 0x1b4, 0xf0,
    0xf8, 0x110, 0x118, 0x128, 0x10 };
  static const uint16_t b2[] = { 0x16c, 0x174, 0x1b2, 0x1c0, 0x150 };
  for (size_t i = 0; i < sizeof b1 / 2; i++) d->cp[b1[i]] = s->cp[b1[i]];
  for (size_t i = 0; i < sizeof b4 / 2; i++) memcpy(d->cp + b4[i], s->cp + b4[i], 4);
  for (size_t i = 0; i < sizeof b8 / 2; i++) memcpy(d->cp + b8[i], s->cp + b8[i], 8);
  for (size_t i = 0; i < sizeof b2 / 2; i++) memcpy(d->cp + b2[i], s->cp + b2[i], 2);
  CPB(d, 0x164) = CPB(s, 0x164); CPI(d, 0x165) = 0x100; CPS(d, 0x169) = 0; CPB(d, 0x16b) = 0;   /* L54807-54811 */
  snprintf(d->scrapname, sizeof d->scrapname, "%s", s->scrapname);                                 /* the CString (Ordinal_1499) */
  ns_pull(d);
}
/* FUN_18004f6d0 L55414: metric - the length defaults x 25.4 */
static void params_metric(NsParams *p) {
  if (!ns_metric) return;
  static const uint16_t o[] = { 0x38, 0x40, 0x48, 0xc8, 0xf0, 0xf8, 0x110, 0x118, 0x128 };
  for (size_t i = 0; i < sizeof o / 2; i++) CPD(p, o[i]) = CPD(p, o[i]) * 25.4;
  ns_pull(p);
}

/* WriteParamsINI FUN_180052240 L56319 / ReadParamsINI FUN_18004f8a0 L55497, key by key in the writer's order. w: D double,
   B Yes/No of a byte, b Yes/No of an int (FUN_1800d4fc0), Y "%d" of a byte, I "%d", H "%hd", C char, S string, U "%lu", P point3,
   X the duplicate-tool block (FUN_180053a10 / FUN_180050fe0). r: the reader's type (- = not read) */
typedef struct { const char *key; char w, r; uint16_t off; } NsPKey;
static const NsPKey PKEYS[] = {
  { "Version", 'D', 'd', 0x138 }, { "TrueShape", 'B', 'b', 0x1f }, { "ScrapName", 'S', 's', 0x130 }, { "AddLabels", 'b', 'i', 0xc0 },
  { "AddPartsAsGroup", 'b', 'i', 0xa4 }, { "AlignWithLongestEdge", 'b', 'i', 0xa8 }, { "AccuNest", 'Y', 'y', 0x1d },
  { "AutoAttachChains", 'b', 'i', 0x88 }, { "AutoAttachSearch", 'b', 'i', 0x8c }, { "AutoOrigins", 'b', 'i', 0xd4 },
  { "AutoPairs", 'b', 'i', 0x5c }, { "CheckForDuplicateScrap", 'B', 'b', 0xb }, { "CommonEdge", 'B', 'b', 0x8 },
  { "CompressResults", 'Y', 'y', 0x1e }, { "CreateBlockOps", 'B', 'b', 0x9 }, { "CycleColors", 'b', 'i', 0x74 },
  { "CycleLevels", 'b', 'i', 0x78 }, { "CycleUnused", 'b', 'i', 0x7c }, { "DeleteChains", 'b', 'i', 0x68 },
  { "DisplayGroupPage", 'b', 'i', 0x90 }, { "DisplayResultsDlg", 'b', 'i', 0x94 }, { "", 'X', 'X', 0x1b4 },
  { "ExactMode", 'Y', 'y', 0x1c }, { "FillAllSheets", 'b', 'i', 0x58 }, { "FitPartInPart", 'b', 'i', 0x50 },
  { "GuillotineCut", 'b', 'i', 0x54 }, { "HighResImages", 'b', 'i', 0x9c }, { "IgnoreHoles", 'b', 'i', 0xac },
  { "LoadDefaultSheet", 'b', 'i', 0x98 }, { "NameAsLabel", 'B', 'b', 0xa }, { "NestFillersToNestHeight", 'b', 'i', 0x60 },
  { "PreferHoleFilling", 'b', 'i', 0x64 }, { "TrimActive", 'B', 'b', 0xc }, { "TrimCombined", 'B', 'b', 0xd },
  { "TrimMinWidth", 'D', 'd', 0x10 }, { "TrimMode", 'I', 'i', 0x18 }, { "SaveScrap", 'b', 'i', 0xa0 },
  { "UseMainColor", 'b', 'i', 0x6c }, { "UseMainLevel", 'b', 'i', 0x70 }, { "LabelHeight", 'D', 'd', 0xc8 },
  { "LabelHeightAuto", 'B', 'b', 0xd0 }, { "OpSelect_include_rapid_moves", 'H', 'h', 0x150 }, { "OpSelect_include_tool_rad", 'B', 'b', 0x152 },
  { "OpSelect_ignore_holes", 'B', '-', 0xac }, { "OnionSkin-Active", 'B', 'b', 0xe8 }, { "OnionSkin-Front", 'B', 'b', 0xe9 },
  { "OnionSkin-Back", 'B', 'b', 0xea }, { "OnionSkin-AreaX", 'D', 'd', 0xf0 }, { "OnionSkin-AreaY", 'D', 'd', 0xf8 },
  { "OnionSkin-IgnoreTabbed", 'B', 'b', 0x100 }, { "OnionSkin-Method", 'I', 'i', 0x104 }, { "OnionSkin-Sort", 'B', 'b', 0x108 },
  { "OnionSkin-Stock", 'D', 'd', 0x110 }, { "OnionSkin-ToolDia", 'D', 'd', 0x118 }, { "OnionSkin-ToolAuto", 'B', 'b', 0x120 },
  { "OnionSkin-Width", 'D', 'd', 0x128 }, { "ParentOpId", 'U', 'u', 0x1ac }, { "PartToPartDist", 'D', 'd', 0x48 },
  { "ProfileTolerance", 'D', 'd', 0x148 }, { "ResCBox", 'I', 'i', 0x28 }, { "Resolution", 'D', 'd', 0x20 },
  { "ResultColor", 'I', 'i', 0x80 }, { "ResultLevel", 'I', 'i', 0x84 }, { "ResUser", 'D', 'd', 0x30 },
  { "ScanForNotesTP", 'b', 'i', 0xb8 }, { "ScanForNotes", 'b', 'i', 0xbc }, { "SeparateOpPerSheet", 'B', 'b', 0x1b0 },
  { "SheetFillDirection", 'C', 'c', 0xd9 }, { "SheetToPartDist", 'D', 'd', 0x40 }, { "SheetToSheetDist", 'D', 'd', 0x38 },
  { "sortGroupByOpIdn", 'B', 'b', 0x171 }, { "sortGroupByRegion", 'B', 'b', 0x172 }, { "sortMaxVacOps", 'B', 'b', 0x16e },
  { "sortMaxVacChains", 'B', 'b', 0x16f }, { "sortGroupBySheet", 'B', 'b', 0x170 }, { "sortMinToolChg", 'B', 'b', 0x1a9 },
  { "sortMode", 'H', 'h', 0x16c }, { "sortRegionOrder", 'H', 'h', 0x174 }, { "sortRegionOverlap", 'D', 'd', 0x188 },
  { "sortRegionX", 'D', 'd', 0x178 }, { "sortRegionY", 'D', 'd', 0x180 }, { "sortStartPt", 'P', 'p', 0x190 },
  { "sortZigZag", 'B', 'b', 0x1a8 }, { "StartingCorner", 'I', 'i', 0xb0 }, { "ToolOriginCorner", 'I', 'i', 0xb4 },
  { "StopBetweenSheets", 'H', 'h', 0x1b2 }, { "CreateChildOps", 'B', 'b', 0x1b1 }, { "xfmCopySourceOps", 'B', 'b', 0x167 },
  { "xfmDisablePosting", 'B', 'b', 0x168 }, { "xfmMethod", 'I', 'i', 0x154 }, { "xfmSubPgmAbs", 'B', 'b', 0x16b },
  { "xfmSubPgmOn", 'B', 'b', 0x169 }, { "xfmSubPgmUnique", 'B', 'b', 0x16a }, { "xfmWoff", 'I', 'i', 0x158 },
  { "xfmWoffCreateNewOps", 'B', 'b', 0x165 }, { "xfmWoffInc", 'I', 'i', 0x160 }, { "xfmWoffKeepExistingOp", 'B', 'b', 0x166 },
  { "xfmWoffMatchExisting", 'B', 'b', 0x164 }, { "xfmWoffStart", 'I', 'i', 0x15c },
};
static const char *const DUPKEYS[13] = { "DupToolChk-MODE", "DupToolChk-TLNO", "DupToolChk-TYPE", "DupToolChk-RADTYPE", "DupToolChk-DIA",
  "DupToolChk-CRAD", "DupToolChk-THDS", "DupToolChk-TIPANGLE", "DupToolChk-DOFF", "DupToolChk-LOFF", "DupToolChk-FLUTES",
  "DupToolChk-MATL", "DupToolChk-STATION" };   /* +0x1b4 (short), then the bytes 0x1b6..0x1c1 */

static void params_write(NsParams *p, NsIni *f) {
  const char *s = "PARAMETERS";
  ns_push(p);
  CPD(p, 0x138) = 14.0;                                                     /* DAT_1801305f0 (L56340) */
  for (size_t i = 0; i < sizeof PKEYS / sizeof PKEYS[0]; i++) {
    const NsPKey *k = &PKEYS[i]; int o = k->off;
    switch (k->w) {
    case 'D': w_dbl(f, s, k->key, CPD(p, o)); break;
    case 'B': w_yes(f, s, k->key, CPB(p, o)); break;
    case 'b': w_ybool(f, s, k->key, CPI(p, o)); break;
    case 'Y': w_int(f, s, k->key, CPB(p, o)); break;
    case 'I': w_int(f, s, k->key, CPI(p, o)); break;
    case 'H': w_short(f, s, k->key, CPS(p, o)); break;
    case 'U': w_ulong(f, s, k->key, (unsigned)CPI(p, o)); break;
    case 'C': w_chr(f, s, k->key, (char)CPB(p, o)); break;
    case 'P': w_pt3(f, s, k->key, &CPD(p, o)); break;
    case 'S':                                                               /* > 1000 characters -> "scrap" (L56356-56363) */
      if (strlen(p->scrapname) > 1000) snprintf(p->scrapname, sizeof p->scrapname, "scrap");
      w_str(f, s, k->key, p->scrapname); break;
    case 'X':                                                               /* FUN_180053a10 L57189 */
      w_short(f, s, DUPKEYS[0], CPS(p, 0x1b4));
      for (int j = 1; j < 13; j++) w_yes(f, s, DUPKEYS[j], CPB(p, 0x1b5 + j));
      break;
    }
  }
}
/* 1 = read (the Version key exists), else -1 with the block at the ctor values. ReadParamsINI L55497-55776: the ctor (copied
   with FUN_18004e650) and the metric scaling first; the specials per key are those of the decompile */
static int params_read(NsParams *p, const NsIni *f) {
  const char *s = "PARAMETERS";
  NsParams c; params_ctor(&c); params_copy(p, &c); params_metric(p); ns_push(p);
  double ver; int has = r_dbl(f, s, "Version", &ver);
  CPD(p, 0x138) = 14.0;
  if (!has) { ns_pull(p); return -1; }
  for (size_t i = 1; i < sizeof PKEYS / sizeof PKEYS[0]; i++) {
    const NsPKey *k = &PKEYS[i]; int o = k->off, got = 0;
    switch (k->r) {
    case 'd': got = r_dbl(f, s, k->key, &CPD(p, o)); break;
    case 'b': got = r_bool(f, s, k->key, &CPB(p, o)); break;
    case 'y': got = r_byte(f, s, k->key, &CPB(p, o)); break;
    case 'i': case 'u': got = r_int(f, s, k->key, &CPI(p, o)); break;
    case 'h': got = r_short(f, s, k->key, &CPS(p, o)); break;
    case 'c': got = r_chr(f, s, k->key, (char *)&CPB(p, o)); break;
    case 'p': got = r_pt3(f, s, k->key, &CPD(p, o)); break;
    case 's': { char b[1100]; if ((got = r_str(f, s, k->key, b, sizeof b))) snprintf(p->scrapname, sizeof p->scrapname, "%s", b); } break;
    case 'X': {                                                             /* FUN_180050fe0 L55780: all 13 or FUN_18004e910(0) */
      int ok = r_short(f, s, DUPKEYS[0], &CPS(p, 0x1b4));
      for (int j = 1; ok && j < 13; j++) ok = r_bool(f, s, DUPKEYS[j], &CPB(p, 0x1b5 + j));
      if (ok) cp_duptool_from_flags(p); else cp_duptool_mode(p, 0);
      got = 1; } break;
    case '-': got = 1; break;
    }
    /* the specials (L55556-55770) */
    if (!got && (o == 0xb || o == 0x1e || o == 0xa || o == 0xc || o == 0xd)) CPB(p, o) = 0;
    if (o == 0x10 && (!got || CPD(p, 0x10) < 0)) CPD(p, 0x10) = 0;
    if (o == 0x18 && !got) CPB(p, 0xc) = 0;
    if (o == 0x58) CPI(p, 0x58) = 0;                                        /* FillAllSheets forced 0 (L55588) */
    if (o == 0x171 && !got) CPB(&P, 0x171) = 0;                             /* DAT_1801379b1 = the live block's (L55705) */
    if (o == 0x167 || o == 0x168 || o == 0x16b || o == 0x169 || o == 0x16a || o == 0x165) CPB(p, o) = 0;
    if (o == 0x166) CPB(p, 0x166) = 1;
    if (o == 0x154 && (unsigned)(CPI(p, 0x154) - 1) > 1) CPI(p, 0x154) = 2;
  }
  ns_pull(p);
  return 1;
}

/* ------------------------------------------------------------------------------------------------ contours */
/* the contour keys (FUN_18001c6a0 / FUN_18001cdd0 write, FUN_18001b390 / FUN_18001b890 read): TYPE, NUM_PTS, XPOS, YPOS, PART_NUM,
   COLOR, LEVEL, WIDTH, STYLE, PSTYLE, DEPTH, VIEW, then per record OBJk (FUN_1800d4310 / FUN_1800d4550): OBJECT, POINT-X/Y, for 'A'
   CENTER-X/Y and CIRCLE, REVERSED, DEPTH. M33 (views > 8) never occurs: chains in Top only */
static void ct_write(NsIni *f, const char *sec, const char *pfx, const NsCtour *c) {
  char k[300];
#define K(x) (snprintf(k, sizeof k, "%s%s", pfx, x), k)
  w_int(f, sec, K("-TYPE"), c->type); w_int(f, sec, K("-NUM_PTS"), c->n);
  w_dbl(f, sec, K("-XPOS"), c->xpos); w_dbl(f, sec, K("-YPOS"), c->ypos); w_int(f, sec, K("-PART_NUM"), c->part_num);
  w_int(f, sec, K("-COLOR"), c->color); w_int(f, sec, K("-LEVEL"), c->level); w_dbl(f, sec, K("-WIDTH"), c->width);
  w_short(f, sec, K("-STYLE"), c->ln_style); w_short(f, sec, K("-PSTYLE"), c->pt_style); w_dbl(f, sec, K("-DEPTH"), c->depth);
  w_short(f, sec, K("-VIEW"), c->view ? c->view : 1);
#undef K
  for (int i = 0; i < c->n; i++) {
    const NsRec *q = &c->r[i]; char o[300], kk[400];
    snprintf(o, sizeof o, "%s-OBJ%d", pfx, i + 1);
#define KO(x) (snprintf(kk, sizeof kk, "%s%s", o, x), kk)
    w_chr(f, sec, KO("-OBJECT"), q->t == 'A' ? 'A' : q->t == 'P' ? 'L' : q->t);
    w_pt(f, sec, KO("-POINT"), q->x, q->y);
    if (q->t == 'A') { w_pt(f, sec, KO("-CENTER"), q->cx, q->cy); w_yes(f, sec, KO("-CIRCLE"), q->circle); }
    w_yes(f, sec, KO("-REVERSED"), q->cw);
    w_dbl(f, sec, KO("-DEPTH"), q->z);
#undef KO
  }
}
static int ct_read(const NsIni *f, const char *sec, const char *pfx, NsCtour *c) {
  char k[300]; int n = 0; short st = 1, ps = 1, vw = 1;
  memset(c, 0, sizeof *c); c->type = 99; c->width = 1.0; c->level = 1; c->color = 10; c->view = 1; c->ln_style = 1; c->pt_style = 1;   /* ctor FUN_18001ac80 */
#define K(x) (snprintf(k, sizeof k, "%s%s", pfx, x), k)
  if (!r_int(f, sec, K("-TYPE"), &c->type) || !r_int(f, sec, K("-NUM_PTS"), &n) || !r_dbl(f, sec, K("-XPOS"), &c->xpos)
      || !r_dbl(f, sec, K("-YPOS"), &c->ypos) || !r_int(f, sec, K("-PART_NUM"), &c->part_num) || !r_int(f, sec, K("-COLOR"), &c->color)
      || !r_int(f, sec, K("-LEVEL"), &c->level) || !r_dbl(f, sec, K("-WIDTH"), &c->width) || !r_short(f, sec, K("-STYLE"), &st)
      || !r_short(f, sec, K("-PSTYLE"), &ps) || !r_dbl(f, sec, K("-DEPTH"), &c->depth) || !r_short(f, sec, K("-VIEW"), &vw)) return 0;
#undef K
  c->ln_style = st; c->pt_style = ps; c->view = vw;
  c->r = calloc((size_t)(n + 1), sizeof(NsRec)); c->n = n;
  for (int i = 0; i < n; i++) {
    NsRec *q = &c->r[i]; char o[300], kk[400]; uint8_t b;
    snprintf(o, sizeof o, "%s-OBJ%d", pfx, i + 1);
#define KO(x) (snprintf(kk, sizeof kk, "%s%s", o, x), kk)
    if (!r_chr(f, sec, KO("-OBJECT"), &q->t) || !r_pt(f, sec, KO("-POINT"), &q->x, &q->y)) return 0;
    if (q->t == 'A') { if (!r_pt(f, sec, KO("-CENTER"), &q->cx, &q->cy) || !r_bool(f, sec, KO("-CIRCLE"), &b)) return 0; q->circle = b; }
    else { q->cx = q->cy = 0; q->circle = 0; }
    if (!r_bool(f, sec, KO("-REVERSED"), &b)) return 0; q->cw = b;
    if (!r_dbl(f, sec, KO("-DEPTH"), &q->z)) return 0;
#undef KO
  }
  if (c->view > 8) return 0;                                               /* M33: views other than the reserved ones - NOT PORTED */
  return 1;
}

/* ------------------------------------------------------------------------------------------------ sheets / parts / groups */
static void sheet_ctor(NsSheet *s) {   /* CSheet ctor FUN_180089fa0 L92146 */
  memset(s, 0, sizeof *s);
  snprintf(s->name, sizeof s->name, "def_sheet"); s->len = 96; s->wid = 48; s->qty = 1; s->prio = 1; s->mmx = 96; s->mmy = 48;
}
/* WriteSheetINI FUN_180091410 L97297 (the contours inserted after CONTOURS-NUMBER by FUN_1800cf900 / FUN_1800cf7c0: the same place
   the sequential writes give); no attached notes here (NOTES-NUMBER 0) */
static void sheet_write(NsSheet *s, NsIni *f, int n) {
  char sec[32]; if (n < 1) snprintf(sec, sizeof sec, "SHEET"); else snprintf(sec, sizeof sec, "SHEET%d", n);
  w_dbl(f, sec, "Version", 14.0); w_int(f, sec, "SheetID", s->id);
  if (s->name[0]) w_str(f, sec, "SheetName", s->name);
  if (s->file[0]) w_str(f, sec, "FileName", s->file);
  if (s->fileext[0]) w_str(f, sec, "FileExt", s->fileext);
  if (s->material[0]) w_str(f, sec, "Material", s->material);
  w_dbl(f, sec, "Length", s->len); w_dbl(f, sec, "Width", s->wid); w_ybool(f, sec, "AddedRectangular", s->added_rect);
  w_pt(f, sec, "OriginPoint", s->ox, s->oy); w_int(f, sec, "Priority", s->prio); w_int(f, sec, "Quantity", s->qty);
  w_chr(f, sec, "GrainDirection", P.grain);                                /* DAT_180137918 (L97397) */
  w_ybool(f, sec, "DelGeo", s->delgeo); w_pt(f, sec, "BasePoint", s->basex, s->basey); w_pt(f, sec, "MinMax", s->mmx, s->mmy);
  w_ybool(f, sec, "AMAR", s->amar); w_int(f, sec, "Contours-Number", s->nct);
  for (int k = 0; k < s->nct; k++) { char pf[32]; snprintf(pf, sizeof pf, "Contour%d", k + 1); ct_write(f, sec, pf, &s->ct[k]); }
  w_int(f, sec, "Notes-Number", 0);
}
/* ReadSheetINI FUN_18008e610 L95329: 0 when neither SheetName nor FileName is there */
static int sheet_read(NsSheet *s, const NsIni *f, int n) {
  char sec[32]; if (n < 1) snprintf(sec, sizeof sec, "SHEET"); else snprintf(sec, sizeof sec, "SHEET%d", n);
  int hasfile = r_str(f, sec, "FileName", s->file, sizeof s->file); if (!hasfile) s->file[0] = 0;
  if (!r_str(f, sec, "SheetName", s->name, sizeof s->name)) { snprintf(s->name, sizeof s->name, "def_sheet"); if (!hasfile) return 0; }
  if (!r_int(f, sec, "SheetID", &s->id)) s->id = sheet_counter++;
  if (!r_str(f, sec, "FileExt", s->fileext, sizeof s->fileext)) s->fileext[0] = 0;
  if (!r_str(f, sec, "Material", s->material, sizeof s->material)) s->material[0] = 0;
  if (!r_dbl(f, sec, "Length", &s->len)) s->len = 96.0;
  if (!r_dbl(f, sec, "Width", &s->wid)) s->wid = 48.0;
  if (!r_int(f, sec, "AddedRectangular", &s->added_rect)) s->added_rect = 0;
  if (!r_pt(f, sec, "OriginPoint", &s->ox, &s->oy)) s->ox = s->oy = 0;
  if (!r_int(f, sec, "Priority", &s->prio) || s->prio < 1) s->prio = 1;
  if (!r_int(f, sec, "Quantity", &s->qty)) s->qty = 1;
  if (!r_chr(f, sec, "GrainDirection", &P.grain)) P.grain = 'N';            /* the live DAT_180137918 (L95450-95453) */
  if (!r_int(f, sec, "DelGeo", &s->delgeo)) s->delgeo = 0;
  if (!r_pt(f, sec, "BasePoint", &s->basex, &s->basey)) s->basex = s->basey = 0;
  if (!r_pt(f, sec, "MinMax", &s->mmx, &s->mmy)) { s->mmx = s->len; s->mmy = s->wid; }
  if (!r_int(f, sec, "AMAR", &s->amar)) s->amar = 0;
  int nc = 0;
  if (!r_int(f, sec, "Contours-Number", &nc) || nc == 0) s->added_rect = 1;
  else for (int k = 0; k < nc; k++) {                                       /* FUN_18001b890 per contour (the lines after the count) */
    char pf[32]; snprintf(pf, sizeof pf, "Contour%d", k + 1);
    NsCtour c; if (!ct_read(f, sec, pf, &c)) { free(c.r); return 0; }
    s->ct = realloc(s->ct, sizeof(NsCtour) * (size_t)(s->nct + 1)); s->ct[s->nct++] = c;
  }
  return 1;   /* NOTES-NUMBER: attached notes are not kept (notes scan NOT PORTED) */
}
static void part_ctor(NsPart *p) {   /* CPart ctor FUN_1800540f0 L57434 */
  memset(p, 0, sizeof *p);
  snprintf(p->name, sizeof p->name, "def_part"); snprintf(p->label, sizeof p->label, "P1");
  p->len = p->wid = 1.0; p->minq = 20; p->step = 90.0; p->prio = 1; p->grain = 'N'; p->cluster = 7; p->mmx = p->mmy = 1.0;
  p->ptol = ns_metric ? 0.1 : 0.01; p->previd = -1;
}
static int part_counter = 1;   /* DAT_180136e80 */
/* WritePartINI FUN_180060d20 L66640 (Version) + FUN_180061b90 L67086 + FUN_1800611d0 (contours) + FUN_180061990 (notes); the debug
   keys of FUN_180061330 only with DAT_180135290 (never) */
static void part_write(NsPart *p, NsIni *f, int n) {
  char sec[32]; if (n < 1) snprintf(sec, sizeof sec, "PART"); else snprintf(sec, sizeof sec, "PART%d", n);
  w_dbl(f, sec, "Version", 14.0); w_int(f, sec, "PartID", p->id); w_int(f, sec, "PrevID", p->previd);
  if (p->file[0]) w_str(f, sec, "FileName", p->file);
  if (p->fileext[0]) w_str(f, sec, "FileExt", p->fileext);
  if (p->material[0]) w_str(f, sec, "Material", p->material);
  if (p->name[0]) w_str(f, sec, "PartName", p->name);
  if (p->label[0]) w_str(f, sec, "PartLabel", p->label);
  w_dbl(f, sec, "Length", p->len); w_dbl(f, sec, "Width", p->wid);
  w_ybool(f, sec, "AddedRectangular", p->added_rect); w_ybool(f, sec, "UserRectangular", p->user_rect);
  w_int(f, sec, "MinQuantity", p->minq); w_int(f, sec, "FillQuantity", p->fillq); w_dbl(f, sec, "StepAngle", p->step);
  w_int(f, sec, "Priority", p->prio); w_ybool(f, sec, "CommonTool", p->commontool); w_ybool(f, sec, "Mirror", p->mirror);
  w_chr(f, sec, "GrainDirection", p->grain); w_int(f, sec, "ClusterMethod", p->cluster); w_ybool(f, sec, "DelGeo", p->delgeo);
  w_pt(f, sec, "BasePoint", p->basex, p->basey); w_pt(f, sec, "MinMax", p->mmx, p->mmy);
  w_int(f, sec, "Contours-Number", p->nct);
  for (int k = 0; k < p->nct; k++) { char pf[32]; snprintf(pf, sizeof pf, "Contour%d", k + 1); ct_write(f, sec, pf, &p->ct[k]); }
  w_int(f, sec, "Notes-Number", 0);
}
/* ReadPartINI FUN_18005c3e0 L63482: 0 when neither PartName nor FileName is there */
static int part_read(NsPart *p, const NsIni *f, int n) {
  char sec[32]; if (n < 1) snprintf(sec, sizeof sec, "PART"); else snprintf(sec, sizeof sec, "PART%d", n);
  int hasfile = r_str(f, sec, "FileName", p->file, sizeof p->file); if (!hasfile) p->file[0] = 0;
  if (!r_str(f, sec, "PartName", p->name, sizeof p->name)) { snprintf(p->name, sizeof p->name, "undefined"); if (!hasfile) return 0; }
  if (!r_int(f, sec, "PartID", &p->id)) p->id = part_counter++;
  if (!r_int(f, sec, "PrevID", &p->previd)) p->previd = -1;
  if (!r_str(f, sec, "FileExt", p->fileext, sizeof p->fileext)) p->fileext[0] = 0;
  if (!r_str(f, sec, "Material", p->material, sizeof p->material)) p->material[0] = 0;
  if (!r_str(f, sec, "PartLabel", p->label, sizeof p->label)) snprintf(p->label, sizeof p->label, "undefined");
  if (!r_dbl(f, sec, "Length", &p->len)) p->len = 1.0;
  if (!r_dbl(f, sec, "Width", &p->wid)) p->wid = 1.0;
  if (!r_int(f, sec, "AddedRectangular", &p->added_rect)) p->added_rect = 0;
  if (!r_int(f, sec, "UserRectangular", &p->user_rect)) p->user_rect = 0;
  if (!r_int(f, sec, "MinQuantity", &p->minq)) p->minq = 10;
  if (!r_int(f, sec, "FillQuantity", &p->fillq)) p->fillq = 0;
  if (!r_dbl(f, sec, "StepAngle", &p->step)) p->step = 90.0;
  if (!r_int(f, sec, "Priority", &p->prio) || p->prio < 1) p->prio = 1;
  if (!r_int(f, sec, "CommonTool", &p->commontool)) p->commontool = 0;
  if (!r_int(f, sec, "Mirror", &p->mirror)) p->mirror = 0;
  if (!r_chr(f, sec, "GrainDirection", &p->grain)) p->grain = 'N';
  if (!r_int(f, sec, "ClusterMethod", &p->cluster)) p->cluster = 7;
  if (!r_int(f, sec, "DelGeo", &p->delgeo)) p->delgeo = 0;
  if (!r_pt(f, sec, "BasePoint", &p->basex, &p->basey)) p->basex = p->basey = 0;
  if (!r_pt(f, sec, "MinMax", &p->mmx, &p->mmy)) { p->mmx = p->len; p->mmy = p->wid; }
  int nc = 0;
  if (!r_int(f, sec, "Contours-Number", &nc) || nc == 0) p->added_rect = 1;
  else for (int k = 0; k < nc; k++) {
    char pf[32]; snprintf(pf, sizeof pf, "Contour%d", k + 1);
    NsCtour c; if (!ct_read(f, sec, pf, &c)) { free(c.r); break; }       /* a contour that fails is dropped (L63680-63684) */
    p->ct = realloc(p->ct, sizeof(NsCtour) * (size_t)(p->nct + 1)); p->ct[p->nct++] = c;
  }
  return 1;
}
typedef struct {          /* CGroup (0x5e0): only the defaults' [GROUP1] is kept (groups NOT PORTED) */
  int id; char name[256]; int status, minq, fillq, prio; int pstatus[100], pqty[100], pid[100];
} NsGroup;
static NsGroup Gdef;
static void group_ctor(NsGroup *g) {   /* FUN_18001da40 L22898 */
  memset(g, 0, sizeof *g); snprintf(g->name, sizeof g->name, "def_group"); g->status = 2; g->minq = 20; g->fillq = 0; g->prio = 1;
  for (int i = 0; i < 100; i++) { g->pstatus[i] = -1; g->pqty[i] = 1; g->pid[i] = -1; }
}
static void group_write(const NsGroup *g, NsIni *f, int n) {   /* FUN_18001e6b0 L23480 */
  char sec[32], k[32]; if (n < 1) snprintf(sec, sizeof sec, "GROUP"); else snprintf(sec, sizeof sec, "GROUP%d", n);
  w_dbl(f, sec, "Version", 14.0); w_int(f, sec, "GroupID", g->id);
  if (g->name[0]) w_str(f, sec, "GroupName", g->name);
  w_int(f, sec, "GroupStatus", g->status); w_int(f, sec, "MinQuantity", g->minq); w_int(f, sec, "FillQuantity", g->fillq);
  w_int(f, sec, "Priority", g->prio);
  for (int i = 0; i < 100; i++) {
    snprintf(k, sizeof k, "PartStatus%d", i + 1); w_int(f, sec, k, g->pstatus[i]);
    snprintf(k, sizeof k, "PartQty%d", i + 1); w_int(f, sec, k, g->pqty[i]);
    snprintf(k, sizeof k, "PartID%d", i + 1); w_int(f, sec, k, g->pid[i]);
    if (g->pid[i] < 0) break;
  }
}
static int group_read(NsGroup *g, const NsIni *f, int n) {   /* FUN_18001e0b0 L23223 */
  char sec[32], k[32]; double v; if (n < 1) snprintf(sec, sizeof sec, "GROUP"); else snprintf(sec, sizeof sec, "GROUP%d", n);
  if (!r_dbl(f, sec, "Version", &v)) return -1;
  if (!r_int(f, sec, "GroupID", &g->id)) return 0;
  if (!r_str(f, sec, "GroupName", g->name, sizeof g->name)) snprintf(g->name, sizeof g->name, "def_group");
  if (!r_int(f, sec, "GroupStatus", &g->status) || !r_int(f, sec, "MinQuantity", &g->minq) || !r_int(f, sec, "FillQuantity", &g->fillq)
      || !r_int(f, sec, "Priority", &g->prio)) return 0;
  for (int i = 0; i < 100; i++) { g->pstatus[i] = -1; g->pqty[i] = 1; g->pid[i] = -1; }
  for (int i = 0; i < 100; i++) {
    snprintf(k, sizeof k, "PartStatus%d", i + 1); if (!r_int(f, sec, k, &g->pstatus[i])) return 0;
    snprintf(k, sizeof k, "PartQty%d", i + 1); if (!r_int(f, sec, k, &g->pqty[i])) return 0;
    snprintf(k, sizeof k, "PartID%d", i + 1); if (!r_int(f, sec, k, &g->pid[i])) return 0;
    if (g->pid[i] < 0) break;
  }
  return 1;
}

/* ------------------------------------------------------------------------------------------------ defaults (.defx) */
static void ns_defaults_path(char *out, size_t n) {   /* "nesting" / "nesting_mm" + add_prefix_suffix(data folder, ".defx") */
  snprintf(out, n, "%s", ns_metric ? "nesting_mm" : "nesting"); ns_add_prefix_suffix(out, n, ns_data_dir(), ".defx");
}
/* FUN_18003ad10 L40636: the defaults - the ctors, then [PARAMETERS] / [SHEET1] (else [SHEET]) / [PART1] / [GROUP1] of the data
   folder's nesting(_mm).defx when it exists; the defaults copied to the live block. Ours: a missing file is first written with
   the shipped file's text (cui_nest_defx.inc), as Mastercam's installation provides it */
static int ns_load_defaults(void) {
  char path[1200]; ns_defaults_path(path, sizeof path);
  if (!ns_file_exists(path)) { FILE *fp = fopen(path, "wb"); if (fp) { NsIni t; ini_text(&t, ns_metric ? NS_DEFX_MM : NS_DEFX_INCH);
      fclose(fp); ini_save(&t, path); ini_free(&t); } }
  NsParams c; params_ctor(&c); params_copy(&Pdef, &c); params_metric(&Pdef);
  ct_free(Sdef.ct, Sdef.nct); sheet_ctor(&Sdef);
  ct_free(Pdpart.ct, Pdpart.nct); part_ctor(&Pdpart);
  group_ctor(&Gdef);
  int ok = 0; NsIni f;
  if (ini_load(&f, path)) {
    ns_prompt_begin(path, 0);
    if (params_read(&Pdef, &f) == 1) {
      int sok = sheet_read(&Sdef, &f, 1);
      if (!sok) { ct_free(Sdef.ct, Sdef.nct); sheet_ctor(&Sdef); sok = sheet_read(&Sdef, &f, -1); }
      Sdef.file[0] = 0;                                                     /* Ordinal_1502(&DAT_1801382b0) */
      if (sok && part_read(&Pdpart, &f, 1)) {
        ct_free(Pdpart.ct, Pdpart.nct); Pdpart.ct = NULL; Pdpart.nct = 0;   /* FUN_180058030 / FUN_1800580d0: the contours go */
        ok = group_read(&Gdef, &f, 1) == 1;
      }
    }
    ini_free(&f);
    ns_prompt_end(path, 0, ok, 0);
  }
  params_copy(&P, &Pdef);                                                   /* FUN_18004e650(live, defaults) L40777 */
  return ok;
}
/* FUN_1800407a0 L44512: the defaults file rewritten (removed first) - [PARAMETERS] of the defaults block, [SHEET1], [PART1], [GROUP1] */
static int ns_write_defaults(void) {
  char path[1200]; ns_defaults_path(path, sizeof path);
  remove(path);
  ns_prompt_begin(path, 1);
  NsIni f; memset(&f, 0, sizeof f);
  params_write(&Pdef, &f); sheet_write(&Sdef, &f, 1); part_write(&Pdpart, &f, 1); group_write(&Gdef, &f, 1);
  int ok = ini_save(&f, path); ini_free(&f);
  ns_prompt_end(path, 1, ok, 1);
  return ok;
}

/* ------------------------------------------------------------------------------------------------ sessions (.nstx) */
static char ns_session_name[1100] = "t";   /* DAT_180136670 ("t" after FUN_180040180, the literal @1800f3b7c) */
static int ns_session_named;               /* DAT_1801352a8 */
/* FUN_180040c50 L44785: the session file (removed first): [PARAMETERS] (live), [SHEETn], [PARTn], [GROUPn] */
static int ns_session_write(const char *name) {
  char path[1200]; snprintf(path, sizeof path, "%s", name); ns_add_prefix_suffix(path, sizeof path, ns_data_dir(), ".nstx");
  if (ns_file_exists(path)) remove(path);
  ns_prompt_begin(path, 1);
  NsIni f; memset(&f, 0, sizeof f);
  params_write(&P, &f);
  for (int i = 0; i < nS; i++) sheet_write(&S[i], &f, i + 1);
  for (int i = 0; i < nP; i++) part_write(&Pt[i], &f, i + 1);
  int ok = ini_save(&f, path); ini_free(&f);
  ns_prompt_end(path, 1, ok, 1);
  return ok;
}
/* FUN_180040bc0 L44713: add_prefix_suffix(".nstx"), the extension must end in 'x' (FUN_1800d97f0), then FUN_180040c50 */
static int ns_session_save(char *name, size_t n) {
  ns_add_prefix_suffix(name, n, ns_data_dir(), ".nstx");
  size_t l = strlen(name); const char *dot = strrchr(name, '.');
  if (!dot || l < 1 || tolower((unsigned char)name[l - 1]) != 'x') { ns_msg("Warning - Invalid file type"); return 0; }   /* FUN_1800d97f0: 10034 [inferred: its message] */
  return ns_session_write(name);
}
static NsSheet *sheet_push(void); static NsPart *part_push(void); static int part_next_id(void);
/* FUN_180040180 L44299: the session cleared - groups, sheets, parts; the defaults copied to the live block; the counters 1; the
   session file name "t", not named */
static void ns_session_reset(void) {
  for (int i = 0; i < nS; i++) ct_free(S[i].ct, S[i].nct);
  for (int i = 0; i < nP; i++) ct_free(Pt[i].ct, Pt[i].nct);
  nS = nP = 0; sel_sheet = sel_part = -1;
  params_copy(&P, &Pdef);
  part_counter = 1; sheet_counter = 1;
  snprintf(ns_session_name, sizeof ns_session_name, "t"); ns_session_named = 0;
}
/* FUN_18003b270 L40852 -> FUN_18003b6b0 L41090 (".nstx"; a name not ending in 'x' is the legacy binary .nst: NOT DECODED, refused):
   [PARAMETERS] into a fresh block then the live one (FUN_18004e650); [SHEETn] / [PARTn] appended until 5 sections in a row are
   missing (each sheet a new id DAT_180136e84; each part PREVID = its PartID, a new PartID FUN_180039610); groups not kept; every
   part's PREVID -1 at the end. CLEAR (param_2): the sheets are dropped first */
static int ns_session_read(char *name, size_t n, int clear) {
  ns_add_prefix_suffix(name, n, ns_data_dir(), ".nstx"); ns_add_prefix_suffix(name, n, ns_data_dir(), ".nst");
  size_t l = strlen(name);
  if (l < 1 || tolower((unsigned char)name[l - 1]) != 'x') { ns_msg("Warning - Invalid file type"); return 0; }   /* the binary reader FUN_18004f760: NOT DECODED (ours: 10034) */
  NsIni f;
  if (!ini_load(&f, name)) { if (!clear) ns_prompt_end(name, 0, 0, 1); return 0; }
  ns_prompt_begin(name, 0);
  NsParams t; params_ctor(&t);
  int ok = params_read(&t, &f) == 1;
  if (clear) { for (int i = 0; i < nS; i++) ct_free(S[i].ct, S[i].nct); nS = 0; }
  params_copy(&P, &t);
  for (int k = 1, miss = 0; miss < 5; k++) {
    NsSheet s; sheet_ctor(&s);
    if (sheet_read(&s, &f, k)) { s.id = sheet_counter++; *sheet_push() = s; miss = 0; }   /* L41219-41224 */
    else { ct_free(s.ct, s.nct); miss++; }
  }
  for (int k = 1, miss = 0; miss < 5; k++) {
    NsPart p; part_ctor(&p);
    if (part_read(&p, &f, k)) { p.previd = p.id; p.id = part_next_id(); *part_push() = p; miss = 0; }   /* L41246-41254 */
    else { ct_free(p.ct, p.nct); miss++; }
  }
  ok = 1;                                                                   /* L41285 */
  for (int i = 0; i < nP; i++) Pt[i].previd = -1;                           /* L41291-41300 */
  ini_free(&f);
  ns_prompt_end(name, 0, ok, 1);
  return ok;
}
