/* scene - shared 3D part/toolpath/solid view used by mcamgl and mcamui (OpenGL 3.3) */
#include <GL/glew.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include <limits.h>
#include "scene.h"
#include "toollib.h"
#include "xt_mesh.h"
#include "mcsrfdl.h"
#include "occt_mesh.h"
#include "occt_cad.h"
#include "mcsurf.h"
#include "draft.h"
#include "draft_note.h"
#include "mcam_saveops.h"
#include "paths.h"

int theme;          /* 0 dark (mcamgl), 1 Mastercam-like light */
double stock_lo[3], stock_hi[3]; int have_stock;   /* bounding box of the solids (used as stock) */
/* ------------------------------------------------------------------------------------------------ math */
typedef struct { float m[16]; } M4;
static M4 m4_mul(M4 a, M4 b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static M4 m4_persp(float fovy, float asp, float zn, float zf) {
    M4 r = { { 0 } };
    float f = 1.0f / tanf(fovy / 2);
    r.m[0] = f / asp; r.m[5] = f; r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1; r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}
static M4 m4_ortho(float l, float rr, float b, float t, float n, float f) {
    M4 r = { { 0 } };
    r.m[0] = 2 / (rr - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
    r.m[12] = -(rr + l) / (rr - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n); r.m[15] = 1;
    return r;
}
static M4 m4_lookat(float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez, fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux, sl = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    float vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
    M4 r = { { sx, vx, -fx, 0, sy, vy, -fy, 0, sz, vz, -fz, 0, 0, 0, 0, 1 } };
    r.m[12] = -(sx * ex + sy * ey + sz * ez); r.m[13] = -(vx * ex + vy * ey + vz * ez); r.m[14] = fx * ex + fy * ey + fz * ez;
    return r;
}

/* ------------------------------------------------------------------------------------------------ GL */
static GLuint prog, vao, vbo, dyn_vao, dyn_vbo;
static GLint u_mvp;
static int m4_inv(const float *m, float *inv);
static GLuint gprog; static GLint ug_mvp, ug_ctr, ug_base, ug_org, ug_target, ug_fade, ug_col, ug_axes;
static GLuint mprog, mvao, mvbo; static GLint um_mvp, um_light, um_color, um_tintz, um_zrange, um_flat, um_stip, um_smask; static int mesh_verts;
static GLuint vfao, vfbo; static int vf_n; static float vf_rgb[3]; static double vf_z[2];   /* the verify stock mesh (scene_set_verify) */
/* The shaded face highlight of a surface (formats/ui.md "Shaded face highlight (GAF stipple)"). Mastercam draws a shaded surface
   through gafgr's GrOGLPipe; the face's GrRender carries the flags SetGrRenderAutoHighlighted (bit 1) / SetGrRenderSelected
   (bit 2) and a colour (SetGrRenderColor), all set by MaintainGrRenderSurface (mastercam.dll 0x180322560). A flagged face is
   drawn in the passes of a stipple list (GetStippleIdx 0x10e926d0 -> the material 1 / 2 = UtRender props 0xb / 0xc whose
   stipple index FUN_1801dbc80's prop set gives as StippleHighlight ? 0 / 1 : -1), the lists made by InitStippleData
   (0x180322020 -> FUN_1801c8440) with Glow type 0 (registry GUI\Diagnostics, absent = .bss 0):
     list 0 (auto-highlight): pass 0 = the face lit in its own GrRender colour, pass 1 = flat (0, 0, 0, 0x40) blended,
                              pass 2 = flat auto_highlight_color with alpha 0x80, blended, through mask 0;
     list 1 (selected):       pass 0 = lit in select_color, pass 1 = flat (0, 0, 0, 0xc4) blended through mask 0.
   Mask 0 = the dwords {0, 0x22222222, 0, 0x88888888} x 8 (FUN_1801c8440 0x1801c84a9..0x1801c8532); mask 1 (a picture, read
   by no pass) is not used. */
static const GLuint stipple_mask0[32] = { 0, 0x22222222u, 0, 0x88888888u, 0, 0x22222222u, 0, 0x88888888u, 0, 0x22222222u, 0, 0x88888888u,
                                          0, 0x22222222u, 0, 0x88888888u, 0, 0x22222222u, 0, 0x88888888u, 0, 0x22222222u, 0, 0x88888888u,
                                          0, 0x22222222u, 0, 0x88888888u, 0, 0x22222222u, 0, 0x88888888u };
int (*scene_ent_hl)(int e);                       /* core_ui: bit 0 = the auto-highlighted entity, bit 1 = drawn selected */
int scene_hl_colors[2] = { 15, 14 };              /* config 5000 auto_highlight_color, 5033 select_color (core_ui sets them) */
int scene_hl_stipple = 1;                         /* config 4053 "Use stipple on solids/surfaces?" = StippleHighlight (.data 1) */
int (*scene_sld_hl)(int kind, int i, int f);      /* core_ui: a solid's GAF highlight flags (scene_sld_look): kind 0 = part solid F.solids[i],
                                                     face f of its topology; kind 1 = body i of the solids made here (f = -1) */

static GLuint shader(GLenum t, const char *src) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "shader: %s\n", log); }
    return s;
}
void gl_setup(void) {
    const char *vs = "#version 330 core\nlayout(location=0) in vec3 pos; layout(location=1) in vec3 col;\n"
                     "uniform mat4 mvp; out vec3 c; void main(){ c = col; gl_Position = mvp * vec4(pos,1); }\n";
    const char *fs = "#version 330 core\nin vec3 c; out vec4 o; void main(){ o = vec4(c,1); }\n";
    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs)); glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    u_mvp = glGetUniformLocation(prog, "mvp");
    for (int i = 0; i < 2; i++) {
        GLuint *a = i ? &dyn_vao : &vao, *b = i ? &dyn_vbo : &vbo;
        glGenVertexArrays(1, a); glGenBuffers(1, b);
        glBindVertexArray(*a); glBindBuffer(GL_ARRAY_BUFFER, *b);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    }
    /* lit mesh shader for solids (two-sided headlight). tintz = 1: the Verify stock (formats/simulator.md 9). Its shading is
       the USER-APPROVED LOOK (2026-11-14, "brighter, near pure yellow"): ModuleWorks' stock lighting is NOT DECODED (the managed
       code only toggles GL_LIGHT_MODEL_TWO_SIDE, Rendering.cs / RenderableVerifier.cs; the lights are set natively), so
       up / down faces (the height map's tops) are drawn unshaded in their colour (cut tops = Scheme[0] #FFFF00 exactly) and the
       walls keep a light headlight shade (0.55 + 0.45 d) so the shape reads. The depth tint by zrange (ours) is left in the
       shader; cui_verify.c passes an empty range (off). */
    const char *mvs = "#version 330 core\nlayout(location=0) in vec3 pos; layout(location=1) in vec3 nrm; layout(location=2) in vec3 vcol;\n"
                      "uniform mat4 mvp; out vec3 n; out float wz; out vec3 vc; void main(){ n = nrm; wz = pos.z; vc = vcol; gl_Position = mvp * vec4(pos,1); }\n";
    /* + the GAF face highlight's extra passes (scene_srf_look below): flat = an unlit glColor pass (GrOGLPipe::PrepTriangleState:
       no Materialize, so PrepOpenGLState turns GL_LIGHTING off), stip = glPolygonStipple (PrepOpenGLState 0x10e964eb): the
       fragment is kept where the window-relative pixel (x mod 32, y mod 32) has its bit set in the 32 x 32 mask - row y mod 32
       = 4 bytes = one dword little-endian, byte x / 8, most significant bit first (GL_UNPACK_LSB_FIRST false: gafgr sets no
       pixel store for the stipple). gl_FragCoord is window-relative with the origin at the bottom left, like the stipple. */
    const char *mfs = "#version 330 core\nin vec3 n; in float wz; in vec3 vc; uniform vec3 light; uniform vec4 color; uniform float tintz; uniform vec2 zrange;\n"
                      "uniform int flatc; uniform int stip; uniform uint smask[32]; out vec4 o;\n"
                      "void main(){\n"
                      "  if (stip != 0) { ivec2 p = ivec2(gl_FragCoord.xy); uint b = (smask[p.y & 31] >> uint(8 * ((p.x & 31) >> 3))) & 0xffu;\n"
                      "                   if (((b >> uint(7 - (p.x & 7))) & 1u) == 0u) discard; }\n"
                      "  if (flatc != 0) { o = color; return; }\n"
                      "  float d = abs(dot(normalize(n), light));\n"
                      "  float h = zrange.y > zrange.x ? clamp((wz - zrange.x) / (zrange.y - zrange.x), 0.0, 1.0) : 1.0;\n"
                      "  float t = mix(1.0, 0.45 + 0.55 * h, tintz);\n"
                      "  float sh = tintz > 0.5 ? (abs(normalize(n).z) > 0.99 ? 1.0 : 0.55 + 0.45 * d) : 0.30 + 0.70 * d;\n"
                      "  o = vec4(color.rgb * vc * sh * t, color.a); }\n";
    mprog = glCreateProgram();
    glAttachShader(mprog, shader(GL_VERTEX_SHADER, mvs)); glAttachShader(mprog, shader(GL_FRAGMENT_SHADER, mfs));
    glLinkProgram(mprog);
    um_mvp = glGetUniformLocation(mprog, "mvp"); um_light = glGetUniformLocation(mprog, "light"); um_color = glGetUniformLocation(mprog, "color");
    um_tintz = glGetUniformLocation(mprog, "tintz"); um_zrange = glGetUniformLocation(mprog, "zrange");
    um_flat = glGetUniformLocation(mprog, "flatc"); um_stip = glGetUniformLocation(mprog, "stip"); um_smask = glGetUniformLocation(mprog, "smask");
    glUseProgram(mprog); glUniform1i(um_flat, 0); glUniform1i(um_stip, 0); glUniform1uiv(um_smask, 32, stipple_mask0); glUseProgram(0);
    glGenVertexArrays(1, &mvao); glGenBuffers(1, &mvbo);
    glBindVertexArray(mvao); glBindBuffer(GL_ARRAY_BUFFER, mvbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    glGenVertexArrays(1, &vfao); glGenBuffers(1, &vfbo);       /* the verify stock mesh (+ a vertex colour: the Compare bands) */
    glBindVertexArray(vfao); glBindBuffer(GL_ARRAY_BUFFER, vfbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 36, (void *)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 36, (void *)24);
    glVertexAttrib3f(2, 1, 1, 1);                              /* the other meshes: no colour array -> white (x color) */
    /* infinite grid: a big quad on the grid plane; the fragment shader draws anti-aliased lines at the spacing that
       suits the zoom (base x 10^k, the next level 10x stronger), the X / Y axes, and fades with distance */
    const char *gvs = "#version 330 core\nlayout(location=0) in vec3 pos; layout(location=1) in vec3 col;\n"
                      "uniform mat4 mvp; uniform vec3 ctr; out vec3 wp; void main(){ wp = pos + ctr; gl_Position = mvp * vec4(pos,1); }\n";
    const char *gfs =
        "#version 330 core\nin vec3 wp; uniform float base; uniform vec2 org; uniform vec3 eye; uniform float fade;\n"
        "uniform vec3 col; uniform int axes; out vec4 o;\n"
        /* coverage of a line family of spacing s at this pixel (anti-aliased, 1 px wide) */
        "float line(vec2 p, float s, vec2 dp) { vec2 c = p / s, d = dp / s; vec2 g = abs(fract(c - 0.5) - 0.5) / max(d, vec2(1e-7));"
        "  return 1.0 - min(min(g.x, g.y), 1.0); }\n"
        /* strength from the on-screen gap only (no jumps when the zoom crosses a level): 0 below 4 px apart, full at 80 px */
        "float strength(float gap_px) { return 0.85 * pow(clamp((gap_px - 4.0) / 76.0, 0.0, 1.0), 0.6); }\n"
        "void main(){\n"
        "  vec2 p = wp.xy - org; vec2 dp = fwidth(wp.xy); float px = max(max(dp.x, dp.y), 1e-7);\n"
        "  float k = floor(log(4.0 * px / base) / log(10.0) + 1.0);\n"              /* finest level at least ~4 px apart */
        "  float s0 = base * pow(10.0, k);\n"
        "  float a = 0.0;\n"
        "  for (int i = 0; i < 3; i++) { float s = s0 * pow(10.0, float(i)); a = max(a, line(p, s, dp) * strength(s / px)); }\n"
        "  vec3 c = col;\n"
        "  if (axes != 0) { vec2 q = wp.xy / max(dp, vec2(1e-7)); float ax = 1.0 - min(abs(q.y) / 1.2, 1.0), ay = 1.0 - min(abs(q.x) / 1.2, 1.0);\n"
        "    if (ax > 0.0) { c = mix(c, vec3(0.80, 0.10, 0.10), ax); a = max(a, ax); }\n"
        "    if (ay > 0.0) { c = mix(c, vec3(0.05, 0.55, 0.05), ay); a = max(a, ay); } }\n"
        "  float f = fade > 0.0 ? 1.0 - smoothstep(fade * 0.45, fade, length(wp - eye)) : 1.0;\n"   /* fade with distance from the eye */
        "  o = vec4(c, a * f); if (o.a < 0.004) discard; }\n";
    gprog = glCreateProgram();
    glAttachShader(gprog, shader(GL_VERTEX_SHADER, gvs)); glAttachShader(gprog, shader(GL_FRAGMENT_SHADER, gfs));
    glLinkProgram(gprog);
    { GLint ok = 0; glGetProgramiv(gprog, GL_LINK_STATUS, &ok); if (!ok) { char log[1024]; glGetProgramInfoLog(gprog, sizeof log, NULL, log); fprintf(stderr, "grid shader: %s\n", log); } }
    ug_mvp = glGetUniformLocation(gprog, "mvp"); ug_ctr = glGetUniformLocation(gprog, "ctr"); ug_base = glGetUniformLocation(gprog, "base");
    ug_org = glGetUniformLocation(gprog, "org"); ug_target = glGetUniformLocation(gprog, "eye"); ug_fade = glGetUniformLocation(gprog, "fade");
    ug_col = glGetUniformLocation(gprog, "col"); ug_axes = glGetUniformLocation(gprog, "axes");
    glBindVertexArray(0);
}

/* ------------------------------------------------------------------------------------------------ state */
McamFile F; int loaded;
float ctrx, ctry, ctrz, ext;                     /* model centre (vertices are stored relative to it) */
/* the graphics view as Mastercam's CGraphicsView keeps it (decomp/mastercam_viewrot*.c, set_parms / FUN_1801dd950):
   a view matrix V (rows: screen right, screen up, towards the viewer, in world coordinates; view = V * world), the
   screen centre (vcx, vcy) in view coordinates (+0x6b8 / +0x6c0) and the view height in world units (+0x760).
   The projection is parallel (centre + height, no perspective). */
double vmat[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, vcx, vcy, vht = 250;
int ortho = 1;
int show_geom = 1, show_tp = 1, show_rapid = 1, show_grid = 0, show_tool = 1;   /* grid off at start, Alt+G (Mastercam) */
/* Mastercam defaults: gradient start colour 111 (150,160,170) at the top, end colour 15 (white) at the bottom;
   system origin colour 156 (102,102,102) - overridden from the user's mcamxm.config by mcamui */
float bg_top[3] = { 150 / 255.f, 160 / 255.f, 170 / 255.f }, bg_bot[3] = { 1, 1, 1 }, origin_col[3] = { 102 / 255.f, 102 / 255.f, 102 / 255.f };
int show_info_axes;                                    /* F9 (OnDisplayInfo): axis lines through the origin to infinity */
int show_axes = 1;
GridSet grid = { 5, 5, 0, 0, 500, 0, 2, 8, 1, 0 };   /* Mastercam's point grid (display_sel_grid) at the construction depth */

double grid_z(void) { return grid.at_floor && loaded ? fmin(F.lo.z, 0.0) : 0.0; }

void grid_defaults(int metric) {           /* set_default_grid: 5 / 500 metric, 0.25 / 25 inch, origin 0, snap Always */
    grid.dx = grid.dy = metric ? 5.0 : 0.25; grid.size = metric ? 500.0 : 25.0;
    grid.ox = grid.oy = 0; grid.snap_mode = 2; grid.snap = 0; grid.color = 8;
}

void grid_round(double *x, double *y) {    /* sel_grid_search: nearest multiple of the spacing, from the origin */
    if (grid.dx > 1e-9) *x = grid.ox + floor((*x - grid.ox) / grid.dx + 0.5) * grid.dx;
    if (grid.dy > 1e-9) *y = grid.oy + floor((*y - grid.oy) / grid.dy + 0.5) * grid.dy;
}
int solid_mode = 1;                               /* 0 off, 1 opaque, 2 translucent; config 6030 Translucent? n -> opaque */
/* S-15 (formats/ui.md "Shaded vs Outline Shaded"): which shaded display solid_mode 1 / 2 is = mastercam.dll render_cfg (RENDER_CFG +0):
   3 = Shaded (DoShadeShaded 0x18046d440), 4 = Outline Shaded (DoShadeShadedOutline 0x18046d540); both also set LastShadedMode
   (0x1808a2838), which this value keeps while the display is wireframe (OnShadeToggle 0x180471590 reads it). Start-up: ours, Outline
   Shaded as before (mccore's ReadCfgFile copies render_cfg's mode into LastShadedMode, decomp/units/core_cfgcallers.c:254; mcgeomsld's
   render_cfg .data mode is 4; the start-up order of the first DoShade* is NOT DECODED). shade_transl = gl_fulltime_translucent: kept
   while the display is wireframe (DoShadeWire leaves it; OnShadeTranslucentToggle 0x1804715f0 flips it only with gl_shading_on). */
int shade_cfg = 4, shade_transl;
int scene_render_cfg(void) { return solid_mode ? shade_cfg : 1; }   /* the Dimmed / No Hidden wireframes (render_cfg 2) are not ported */
/* OnShadeWire / OnShadeShaded / OnShadeShadedOutline (cfg 1 / 3 / 4): the display mode, translucency kept */
void scene_shade(int cfg) {
    if (solid_mode) shade_transl = solid_mode == 2;
    if (cfg < 3) { solid_mode = 0; return; }
    shade_cfg = cfg; solid_mode = shade_transl ? 2 : 1;
}
/* OnShadeToggle (Alt+S, 0x180471590): wireframe -> LastShadedMode 4 ? Outline Shaded : Shaded; shaded -> the wireframe of LastWireFlags
   (bit 0 clear -> DoShadeWireHidden, bit 1 clear -> DoShadeWire, else DoShadeWireDimmed; LastWireFlags .data 0, ReadCfgFile copies
   RENDER_CFG +8 into it - its default NOT DECODED, and Dimmed / No Hidden are not ported: plain Wireframe, as before) */
void scene_shade_toggle(void) { scene_shade(solid_mode ? 1 : shade_cfg == 4 ? 4 : 3); }
/* OnShadeTranslucentToggle: only with shading on */
void scene_translucent_toggle(void) { if (!solid_mode) return; solid_mode = solid_mode == 2 ? 1 : 2; shade_transl = solid_mode == 2; }
int scene_force_mesh;                             /* mcamui sets it while the Extrude panel is up: the preview is drawn
                                                     shaded even in Wireframe display (formats/extrude.md sec. 3; how the
                                                     original shades the preview in wireframe mode is NOT DECODED - ours) */
static float *user_edge; static int user_nedge;         /* their edges (body_edges), drawn in the solid colour, unlit */
static float *user_tri; static int user_ntri;           /* solids made here (Solids > Extrude), model units */
static double xt_lo[3], xt_hi[3]; static int xt_stock;  /* bounding box of the part's own solids */
static float *solid_tri; int solid_ntri, solid_faces, solid_meshed, solids_ok; double solid_scale = 1000;
static int *solid_tfirst, *solid_tn, solid_nmeshed;   /* per F.solids[i] at the last mesh_solids (solid_nmeshed of them): its triangles in solid_tri (first, count; 0 = not meshed) */
/* per F.solids[i]: its faces' triangle ranges in solid_tri (sf_first / sf_n from solid_f0[i], solid_nf[i] faces; the face order of the
   solid's topology = xt_mesh / xt_brep_topo's FACE node order, occt_brep_mesh_faces / occt_brep_topo's TopExp order) - the shaded face
   highlight of a solid face (scene_sld_hl) */
static int *solid_f0, *solid_nf, *sf_first, *sf_n, sf_tot;
static int *ubody_n, ubody_cnt;                         /* the solids made here: each body's triangles in user_tri, in order (scene_set_user_bodies) */
/* the surface entities' display mesh (mesh_surfs below) */
static float *surf_tri, *surf_seg; static int surf_ntri, surf_nseg, *surf_tfirst, *surf_tn, surf_nmeshed, surf_nskip;
static GLuint svao, svbo, sevao, sevbo; static int surf_up;
static double surf_wire_wk; static int surf_wire_mode = -1, surf_wire_dlgen = -1;   /* build_surf_wire's last view height / mode */
static int *surf_sfirst, *surf_sn, surf_nwired, surf_wire_gl;               /* each surface's first segment / count in surf_seg (build_surf_wire) */
static int surf_shown(int i);
static int surf_color(int i);
static void build_surf_wire(void);
static int mesh_new_surfs(void);
char solid_msg[160];
int sel = -1;                                    /* -1 = all ops, else index into F.ops */
long sim = -1; int playing; float sim_speed = 2000; /* toolpath points per second */
int bp_tool_on, bp_tool_num; double bp_tool_pos[3]; Tool bp_tool_op;
int bp_lines_on; const float *bp_lv[2]; int bp_ln[2];
int bp_draw_cutter, bp_draw_holder, bp_tool_shaded; float bp_tool_rgba[4] = { 1, 1, 1, 1 }, bp_holder_rgba[4] = { 1, 0, 0, 1 }; double bp_spin_deg;
const float *bp_ep; int bp_nep;
static GLint *firsts; static GLsizei *counts; static int capdraw;
static GLint *gfirst; static int capgfirst;         /* per polyline: first vertex in the GPU buffer (Poly.first indexes F.pts) */
char path_buf[1024]; char status[256];

/* Mastercam's 256-colour palette: mastercam.dll @0x8a6440 (4-byte RGB entries), ../formats/mastercam_palette.tsv */
const unsigned char MC_PALETTE[256][3] = {
    {0,0,0}, {0,0,128}, {0,128,0}, {0,192,192}, {128,0,0}, {128,0,128}, {128,66,0}, {192,192,192},
    {128,128,128}, {0,0,255}, {0,255,0}, {0,255,255}, {255,0,0}, {255,0,255}, {255,255,0}, {255,255,255},
    {255,255,204}, {255,255,153}, {255,255,128}, {255,255,102}, {255,255,51}, {204,255,255}, {204,255,204}, {204,255,153},
    {204,255,102}, {204,255,51}, {204,255,0}, {153,255,255}, {153,255,204}, {153,255,153}, {153,255,102}, {153,255,51},
    {153,255,0}, {102,255,255}, {102,255,204}, {102,255,153}, {102,255,102}, {102,255,51}, {102,255,0}, {51,255,255},
    {51,255,204}, {51,255,153}, {128,255,128}, {51,255,102}, {51,255,51}, {51,255,0}, {0,255,204}, {0,255,153},
    {0,255,102}, {0,255,51}, {255,204,255}, {255,204,204}, {255,204,153}, {255,204,102}, {255,204,51}, {255,204,0},
    {204,204,255}, {204,204,204}, {204,204,153}, {204,204,102}, {204,204,51}, {204,204,0}, {192,192,0}, {153,204,255},
    {153,204,204}, {153,204,153}, {153,204,102}, {153,204,51}, {153,204,0}, {102,204,255}, {102,204,204}, {102,204,153},
    {102,204,102}, {102,204,51}, {102,204,0}, {51,204,255}, {51,204,204}, {51,204,153}, {51,204,102}, {51,204,51},
    {51,204,0}, {0,204,255}, {0,204,204}, {0,204,153}, {0,204,102}, {0,204,51}, {0,204,0}, {0,192,0},
    {0,170,0}, {255,153,255}, {255,153,204}, {255,153,153}, {255,153,102}, {255,153,51}, {255,153,0}, {204,153,255},
    {204,153,204}, {204,153,153}, {204,153,102}, {204,153,51}, {204,153,0}, {153,153,255}, {153,153,204}, {153,153,153},
    {153,153,102}, {153,153,51}, {153,153,0}, {128,128,0}, {102,153,255}, {102,153,204}, {0,128,192}, {150,160,170},
    {0,128,128}, {102,153,102}, {102,153,51}, {102,153,0}, {51,153,255}, {51,153,204}, {51,153,153}, {51,153,102},
    {51,153,51}, {51,153,0}, {0,153,255}, {0,128,64}, {0,153,204}, {0,153,153}, {0,153,102}, {0,153,51},
    {0,153,0}, {255,128,255}, {255,102,255}, {255,128,192}, {255,102,204}, {255,102,153}, {255,128,128}, {255,102,102},
    {255,128,64}, {255,102,51}, {255,102,0}, {204,102,255}, {204,102,204}, {204,102,153}, {204,102,102}, {204,102,51},
    {204,102,0}, {153,102,255}, {153,102,204}, {153,102,153}, {153,102,102}, {153,102,51}, {153,102,0}, {128,128,255},
    {102,102,255}, {128,128,192}, {102,102,204}, {102,102,153}, {102,102,102}, {102,102,51}, {102,102,0}, {0,128,255},
    {51,102,255}, {51,102,204}, {51,102,153}, {51,102,102}, {51,102,51}, {51,102,0}, {0,102,255}, {0,102,204},
    {0,102,153}, {0,102,102}, {0,102,51}, {0,102,0}, {255,51,255}, {255,51,204}, {255,51,153}, {255,51,102},
    {255,51,51}, {255,51,0}, {204,51,255}, {204,51,204}, {204,51,153}, {204,51,102}, {204,51,51}, {204,51,0},
    {153,51,255}, {153,51,204}, {153,51,153}, {153,51,102}, {153,51,51}, {153,51,0}, {102,51,255}, {102,51,204},
    {102,51,153}, {102,51,102}, {128,64,64}, {102,51,51}, {102,51,0}, {51,51,255}, {51,51,204}, {51,51,153},
    {51,51,102}, {64,64,64}, {51,51,51}, {64,64,0}, {51,51,0}, {0,51,255}, {0,51,204}, {0,51,153},
    {0,64,128}, {0,51,102}, {0,64,64}, {0,51,51}, {0,64,0}, {0,51,0}, {255,0,204}, {255,0,153},
    {255,0,102}, {255,0,51}, {204,0,255}, {204,0,204}, {192,0,192}, {204,0,153}, {204,0,102}, {204,0,51},
    {204,0,0}, {192,0,0}, {170,0,0}, {153,0,255}, {153,0,204}, {153,0,153}, {153,0,102}, {153,0,51},
    {153,0,0}, {102,0,255}, {128,0,255}, {102,0,204}, {102,0,153}, {102,0,102}, {128,0,64}, {102,0,51},
    {102,0,0}, {64,0,0}, {51,0,255}, {51,0,204}, {51,0,153}, {64,0,128}, {51,0,102}, {64,0,64},
    {51,0,51}, {51,0,0}, {0,0,204}, {0,0,170}, {0,0,153}, {0,0,102}, {0,0,64}, {0,0,51} };
/* colour numbers from the user's Mastercam configuration (mcamxm.config [COLORS]); mcamui reads them at start-up */
int cfg_feed_color = 159, cfg_rapid_color = 14, cfg_solid_color = 8, cfg_geom_color = 1;   /* 5045, 5046, 5052, 5027 */
int cfg_srf_edge_color = 0;   /* 5054 "Surface edge color?" = surfaceEdgeColor (mastercam.dll 0x1809a0b79, .bss 0; the colour dialog's control
                                0x13be, decomp/mccore_sldchain_misc.c:54): the surfaces' outline in Outline Shaded (build_surf_wire) */
int cfg_outline_color = 0;   /* 8027 "Outlined edge color?" = RENDER_CFG +0x12 (mcgeomsld export render_cfg 0x1803b9c50, .data default 0;
                                its neighbours 8024 / 8025 / 8026 match +0xc 66 / +0x10 10 / +0x11 10) */
void mc_col(int idx, float *c) { idx &= 255; for (int k = 0; k < 3; k++) c[k] = MC_PALETTE[idx][k] / 255.0f; }

int op_index(int id) { for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) return i; return 0; }

int scene_tp_hide_id;                    /* Q20: the op whose toolpath is not drawn (its preview is, as an overlay); 0 = none */
static int keep_view;
static float *ovl; static int novl;     /* overlay lines (x y z r g b pairs, world coords) */
static float *gnt_v, *gnl_v; static int gnt_n, gnl_n;   /* Transform > Dynamic's gnomon (core_ui cui_xdyn.c): triangles x y z nx ny nz r g b a, lines x y z r g b */
static GLuint gn_prog, gn_vao, gn_vbo; static GLint ugn_mvp, ugn_light;
static float *dashv; static int ndash; static float dash_ctr[3];   /* dashed line-style geometry (same layout), stored relative to
                                         dash_ctr (the model centre when set; ours: no per-frame copy); baked by core_ui (screen px) */
/* a geometry polyline is drawn only on a visible level (draw_ent @1803312e0 LevelData::isVisible, mcam_load.h mcam_level_visible) */
#define GEOM_SHOWN(p) (!(p)->blanked && mcam_level_visible(&F, (p)->level))   /* + not blanked (draw_ent's sel bit 0x100 test) */
static int lock_ctr;                     /* a rotation centre chosen with Alt+F12 stays until a new part is loaded */
static void pm_build(void); static int pm_dirty, pm_nm, pm_up;   /* the meshes (below) */
static void upload(void) {
    if (scene_upload_hook) scene_upload_hook();
    /* one vertex buffer for everything: polylines in order (geometry, then toolpaths in Operations Manager order) */
    if (pm_dirty || pm_nm != F.nmeshes) pm_build();  /* the meshes in the model extents first (pm_build) */
    pm_up = 1;                                        /* their buffers follow the centre */
    if (!lock_ctr) { ctrx = (float)((F.lo.x + F.hi.x) / 2); ctry = (float)((F.lo.y + F.hi.y) / 2); ctrz = (float)((F.lo.z + F.hi.z) / 2); }
    ext = (float)fmax(fmax(F.hi.x - F.lo.x, F.hi.y - F.lo.y), F.hi.z - F.lo.z);
    if (ext <= 0) ext = 10;
    float *v = malloc((size_t)(F.npts ? F.npts : 1) * 6 * sizeof(float));
    if (F.npolys > capgfirst) { capgfirst = F.npolys; gfirst = realloc(gfirst, capgfirst * sizeof(GLint)); }
    int k = 0;
    for (int i = 0; i < F.npolys; i++) {
        Poly *p = &F.polys[i];
        float c[3];                               /* geometry: the entity's colour; toolpaths: config feed / rapid colour */
        mc_col(p->kind == K_GEOM ? p->color : p->kind == K_RAPID ? cfg_rapid_color : cfg_feed_color, c);
        int nf = k;
        for (int j = 0; j < p->count; j++) {
            V3 q = F.pts[p->first + j];
            v[k * 6 + 0] = (float)(q.x - ctrx); v[k * 6 + 1] = (float)(q.y - ctry); v[k * 6 + 2] = (float)(q.z - ctrz);
            v[k * 6 + 3] = c[0]; v[k * 6 + 4] = c[1]; v[k * 6 + 5] = c[2];
            k++;
        }
        gfirst[i] = nf;
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)k * 24, v, GL_STATIC_DRAW);
    free(v);
    mesh_verts = (solid_ntri + user_ntri) * 3;
    float *mv = malloc((size_t)(mesh_verts ? mesh_verts : 1) * 24);
    for (int i = 0; i < mesh_verts; i++) {
        memcpy(mv + i * 6, i < solid_ntri * 3 ? solid_tri + i * 6 : user_tri + (i - solid_ntri * 3) * 6, 24);
        mv[i * 6] -= ctrx; mv[i * 6 + 1] -= ctry; mv[i * 6 + 2] -= ctrz;
    }
    glBindBuffer(GL_ARRAY_BUFFER, mvbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)mesh_verts * 24, mv, GL_STATIC_DRAW);
    free(mv);
    if (!svao) {                                      /* the surfaces' faces (x y z nx ny nz) and edges (x y z r g b) */
        glGenVertexArrays(1, &svao); glGenBuffers(1, &svbo); glBindVertexArray(svao); glBindBuffer(GL_ARRAY_BUFFER, svbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
        glGenVertexArrays(1, &sevao); glGenBuffers(1, &sevbo); glBindVertexArray(sevao); glBindBuffer(GL_ARRAY_BUFFER, sevbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    }
    mesh_new_surfs();                                 /* surfaces made here since the last mesh */
    {   float *sv = malloc((size_t)(surf_ntri ? surf_ntri : 1) * 18 * sizeof(float));
        for (int i = 0; i < surf_ntri * 3; i++) { memcpy(sv + i * 6, surf_tri + i * 6, 24); sv[i * 6] -= ctrx; sv[i * 6 + 1] -= ctry; sv[i * 6 + 2] -= ctrz; }
        glBindVertexArray(svao); glBindBuffer(GL_ARRAY_BUFFER, svbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)surf_ntri * 72, sv, GL_STATIC_DRAW); free(sv);
        glBindVertexArray(0); surf_up = 0; surf_wire_mode = -1; }   /* the wireframe is rebuilt at the next draw (build_surf_wire) */
    if (F.npolys > capdraw) { capdraw = F.npolys; firsts = realloc(firsts, capdraw * sizeof(GLint)); counts = realloc(counts, capdraw * sizeof(GLsizei)); }
    if (!keep_view) { view_fit(0); }
}

#define find_schema xt_schema_for

/* stock box = the part's solids and the solids made here */
static void user_stock(void) {
    double lo[3], hi[3]; int any = xt_stock;
    for (int k = 0; k < 3; k++) { lo[k] = xt_stock ? xt_lo[k] : 1e300; hi[k] = xt_stock ? xt_hi[k] : -1e300; }
    for (int v = 0; v < user_ntri * 3; v++) for (int k = 0; k < 3; k++) { double q = user_tri[v * 6 + k]; if (q < lo[k]) lo[k] = q; if (q > hi[k]) hi[k] = q; any = 1; }
    have_stock = any;
    if (any) for (int k = 0; k < 3; k++) { stock_lo[k] = lo[k]; stock_hi[k] = hi[k]; }
}
void scene_set_user_edges(const float *seg, int nseg) {
    free(user_edge); user_edge = NULL; user_nedge = 0;
    if (nseg > 0) { user_edge = malloc((size_t)nseg * 6 * sizeof(float)); memcpy(user_edge, seg, (size_t)nseg * 6 * sizeof(float)); user_nedge = nseg; }
}
void scene_set_user_solids(const float *tri, int ntri) {
    free(user_tri); user_tri = NULL; user_ntri = 0;
    if (ntri > 0) { user_tri = malloc((size_t)ntri * 18 * sizeof(float)); memcpy(user_tri, tri, (size_t)ntri * 18 * sizeof(float)); user_ntri = ntri; }
    user_stock();
    if (user_ntri) for (int v = 0; v < user_ntri * 3; v++) for (int k = 0; k < 3; k++) {     /* in the model extents */
        double q = user_tri[v * 6 + k]; if (q < (&F.lo.x)[k]) (&F.lo.x)[k] = q; if (q > (&F.hi.x)[k]) (&F.hi.x)[k] = q; }
}

/* the solids made here: body b's triangles are n[b] of user_tri, body after body (bodies_show) - their GAF body highlight */
void scene_set_user_bodies(const int *n, int nb) {
    free(ubody_n); ubody_n = NULL; ubody_cnt = 0;
    if (nb > 0 && n && (ubody_n = malloc(sizeof(int) * (size_t)nb))) { memcpy(ubody_n, n, sizeof(int) * (size_t)nb); ubody_cnt = nb; }
}
int scene_part_tris(const float **tri) { *tri = solid_tri; return solid_ntri; }   /* the part solids' mesh (model units) */
int scene_user_tris(const float **tri) { *tri = user_tri; return user_ntri; }    /* the solids made here (model units) */
/* the machine group's shaded stock model (mastercam.dll draw_stock 0x1803360a0 -> FUN_18033fe70, decomp/mastercam_stocksetup.c
   l.5379): glEnable(GL_BLEND), glDepthMask(0), the model's triangles with per-triangle normals (FUN_180136e60:
   (p0 - p1) x (p1 - p2), normalised), material ambient = diffuse = the stock colour (Wd_colors[mill_stock_color]) with
   alpha 0.375 (gl_Copy_Color(.., 0.375), FUN_180229750 GL_FRONT_AND_BACK), specular 0.5 grey, shininess 0.5 * 128. The
   lights (gl_Init_Lights_E) are NOT DECODED: this program's two-sided headlight shader (ours). x y z nx ny nz per vertex. */
static float *stk_v; static int stk_n, stk_up; static float stk_rgba[4], stk_ctr[3]; static GLuint skvao, skvbo;
/* the buffer (x y z nx ny nz per vertex, world) becomes the scene's: stored relative to the model centre in place (ours: no
   copy at set or draw time); NULL / 0 hides the model and frees it */
void scene_take_stock_mesh(float *v, int n, const float rgba[4]) {
    free(stk_v); stk_v = NULL; stk_n = 0;
    if (n > 0 && v) {
        for (int i = 0; i < n; i++) { v[i * 6] -= ctrx; v[i * 6 + 1] -= ctry; v[i * 6 + 2] -= ctrz; }
        stk_v = v; stk_n = n; stk_ctr[0] = ctrx; stk_ctr[1] = ctry; stk_ctr[2] = ctrz;
    } else free(v);
    if (rgba) memcpy(stk_rgba, rgba, sizeof stk_rgba);
    stk_up = 1;
}
/* GetFitExtents (FUN_180320770) adds the active machine group's stock when Display and Fit screen are on
   (FUN_180337650, decomp/mastercam_stock_fitshade.c): the core sets this hook (cui_stock.c stock_fit_points) */
void (*scene_part_changed)(void);
void (*scene_upload_hook)(void);
void (*scene_draw_hook)(void);
int (*scene_srfdl_pts)(int i, const void **rec);
int scene_srfdl_gen;
int scene_part_gen;
void (*scene_part_closing)(void);
void (*scene_fit_extra)(void (*acc)(void *u, double x, double y, double z), void *u);
int scene_part_solid_tris(int i, const float **tri) {   /* F.solids[i]'s triangles (ours: per-solid ranges of the mesh above) */
    if (i < 0 || i >= solid_nmeshed || i >= F.nsolids || !solid_tfirst || !solid_tn) { *tri = NULL; return 0; }
    *tri = solid_tri + (size_t)solid_tfirst[i] * 18; return solid_tn[i];
}
/* the part's surface entities (F.surfs, SURF_ID): an OCCT mesh per surface (occt_mesh_surf: NURBS only), shaded in the entity's
   colour in the shaded / translucent modes, its boundary edges in that colour in every mode. Mastercam's surface display (the
   shading colours, the wireframe flowlines and their density, the tessellation tolerance) is NOT DECODED: ours, as the solids'
   lighting. Deflection: a two-thousandth of the part's diagonal (ours). Kinds other than NURBS / trimmed NURBS are not drawn
   (surf_nskip); blanked surfaces (sel bit 0x100, e.g. every trimmed surface's base) are not drawn. */
int scene_part_surf_tris(int i, const float **tri) {   /* F.surfs[i]'s triangles (x y z nx ny nz per vertex, model units) */
    if (i < 0 || i >= surf_nmeshed || i >= F.nsurfs || !surf_tfirst) { *tri = NULL; return 0; }
    *tri = surf_tri + (size_t)surf_tfirst[i] * 18; return surf_tn[i];
}
int scene_surf_skipped(void) { return surf_nskip; }
/* the surfaces' wireframe (mcsurf.c mcs_wireframe = Mastercam's FUN_180238320 / FUN_180238ee0): flowlines at surf_division's
   spacing, which follows the view (get_wk_height, the pixels per unit); in the shaded modes the outline variant (render_cfg 4:
   untrimmed surfaces their boundary, trimmed ones their loops and flowlines). Rebuilt when the mode changes or the view height has
   changed by more than 1.5 x since the last build (ours: Mastercam rebuilds its display lists on its own events). The system
   tolerance tol = config 1000's default 5e-5 (formats/qt_port_plan.md). Colour: wireframe the entity's (find_color); Outline Shaded
   surfaceEdgeColor = config 5054 (the repaint's per-entity loop FUN_18031f620: a surface in render_cfg 4 -> bVar13 -> FUN_180135ad0
   with surfaceEdgeColor, 0x18031f8fa..0x18031f925). Shaded (render_cfg 3): none - display_ent_wireframe 0x18032fed0 -> FUN_18032ff50
   (gl_shade_all: render_cfg == 4) is false, so the loop and FUN_1801d71d0 skip surfaces and draw_ent_nolist takes FUN_180229d90 (the
   shaded face only). mode: 0 wireframe, 1 outline, 2 none.
   Outline Shaded (S-17): what the loop draws is the surface's display-list node (FUN_180135ad0 case 8 -> FUN_18013cf90: the records'
   polylines, each from a flag-0 record to the next) - the render_cfg 4 variant when the node was built in render_cfg 4: an untrimmed
   surface its boundary (FUN_18013a130), a trimmed one ONLY its loops (FUN_18013a5e0, decomp/chain_srfedge_dl2.c:75-85: render_cfg 4
   and ent +0x6e bits 1 / 2 clear -> return after FUN_1801395e0), cui_srfdl.c keeps them (mcsrfdl.c, oracle-checked). A surface whose node
   is not built goes to draw_ent_nolist (FUN_18031f620: local_2a00[0x10] == 0) -> FUN_18021e020 = mcs_wireframe in the entity's colour. */
static void build_surf_wire(void) {
    int mode = solid_mode == 0 ? 0 : shade_cfg == 4 ? 1 : 2;
    if (surf_wire_mode == mode && surf_wire_wk > 0 && vht / surf_wire_wk < 1.5 && surf_wire_wk / vht < 1.5 && (mode != 1 || surf_wire_dlgen == scene_srfdl_gen)) return;
    surf_wire_mode = mode; surf_wire_wk = vht; surf_wire_dlgen = scene_srfdl_gen;
    free(surf_seg); surf_seg = NULL; surf_nseg = 0;
    free(surf_sfirst); free(surf_sn); surf_sfirst = calloc(F.nsurfs ? F.nsurfs : 1, sizeof(int)); surf_sn = calloc(F.nsurfs ? F.nsurfs : 1, sizeof(int)); surf_nwired = F.nsurfs;
    double xs = view_h > 0 && vht > 0 ? view_h / vht : 1.0;
    int cap = 0;
    for (int i = 0; i < F.nsurfs; i++) {
        surf_sfirst[i] = surf_nseg;
        if (mode == 2 || !surf_shown(i) || !surf_tn || i >= surf_nmeshed || !surf_tn[i]) continue;
        float *p = NULL; int *cnt = NULL, np = 0, dl = 0;
        const void *rv = NULL; int nr = mode == 1 && scene_srfdl_pts ? scene_srfdl_pts(i, &rv) : 0;
        if (nr > 0) {                                /* the node's records as polylines (a flag-0 record starts one) */
            const SdlRec *r = rv; dl = 1;
            p = malloc((size_t)nr * 3 * sizeof(float)); cnt = malloc((size_t)nr * sizeof(int));
            for (int k = 0; k < nr; k++) {
                if (k == 0 || r[k].flag == 0) cnt[np++] = 0;
                memcpy(p + 3 * k, r[k].p, 12); cnt[np - 1]++;
            }
        } else mcs_wireframe(&F, &F.surfs[i], mode, vht, xs, xs, 5e-5, &p, &cnt, &np);
        float c[3]; mc_col(dl ? cfg_srf_edge_color : surf_color(i), c);
        int at = 0;
        for (int k = 0; k < np; k++) {
            for (int q = 0; q + 1 < cnt[k]; q++) {
                if (surf_nseg == cap) { cap = cap ? cap * 2 : 4096; surf_seg = realloc(surf_seg, (size_t)cap * 12 * sizeof(float)); }
                float *o = surf_seg + (size_t)surf_nseg * 12;
                for (int e = 0; e < 2; e++) { const float *v = p + 3 * (at + q + e); o[e * 6] = v[0]; o[e * 6 + 1] = v[1]; o[e * 6 + 2] = v[2]; o[e * 6 + 3] = c[0]; o[e * 6 + 4] = c[1]; o[e * 6 + 5] = c[2]; }
                surf_nseg++;
            }
            at += cnt[k];
        }
        free(p); free(cnt);
        surf_sn[i] = surf_nseg - surf_sfirst[i];
    }
    surf_wire_gl = 1;                                 /* uploaded by the next draw (this may run without the GL context: picking) */
}
static void upload_surf_wire(void) {
    if (!surf_wire_gl || !sevao) return;
    surf_wire_gl = 0;
    float *ev = malloc((size_t)(surf_nseg ? surf_nseg : 1) * 12 * sizeof(float));
    for (int i = 0; i < surf_nseg * 2; i++) { memcpy(ev + i * 6, surf_seg + i * 6, 24); ev[i * 6] -= ctrx; ev[i * 6 + 1] -= ctry; ev[i * 6 + 2] -= ctrz; }
    glBindVertexArray(sevao); glBindBuffer(GL_ARRAY_BUFFER, sevbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)surf_nseg * 48, ev, GL_STATIC_DRAW); free(ev);
}
/* one surface's display triangles, appended to surf_tri (ctri: its capacity) */
static int surf_ctri;
static void mesh_one_surf(int i, double defl) {
    surf_tfirst[i] = surf_ntri; surf_tn[i] = 0;
    if (F.surfs[i].blanked) return;                  /* blanked (sel bit 0x100): not drawn, not picked */
    double *t = NULL; char e[160];
    int nt = mcs_mesh(&F, &F.surfs[i], defl, &t, NULL, NULL, e, sizeof e);
    if (nt < 0) { surf_nskip++; return; }
    if (surf_ntri + nt > surf_ctri) { surf_ctri = (surf_ntri + nt) * 2; surf_tri = realloc(surf_tri, (size_t)surf_ctri * 18 * sizeof(float)); }
    for (int k = 0; k < nt; k++) {
        const double *a = t + 9 * k, *b = a + 3, *d = a + 6;
        double u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { d[0] - a[0], d[1] - a[1], d[2] - a[2] };
        double nx = u[1] * v[2] - u[2] * v[1], ny = u[2] * v[0] - u[0] * v[2], nz = u[0] * v[1] - u[1] * v[0], l = sqrt(nx * nx + ny * ny + nz * nz);
        if (l > 0) { nx /= l; ny /= l; nz /= l; }
        float *o = surf_tri + (size_t)(surf_ntri + k) * 18;
        for (int q = 0; q < 3; q++) { o[q * 6] = (float)a[3 * q]; o[q * 6 + 1] = (float)a[3 * q + 1]; o[q * 6 + 2] = (float)a[3 * q + 2];
                                      o[q * 6 + 3] = (float)nx; o[q * 6 + 4] = (float)ny; o[q * 6 + 5] = (float)nz; }
    }
    surf_ntri += nt; surf_tn[i] = nt;
    free(t);
}
static int surf_color(int i) { int e = F.surfs[i].ent; return e >= 0 && e < F.nents ? F.ents[e].color : F.surfs[i].color; }   /* the entity's (a colour change here) */
/* one GAF face drawn with its look (bits as scene_srf_look: 1 AutoHighlighted, 2 Selected, 4 StippleHighlight), the mesh program and the
   face's vertex array bound: pass 0 = lit in GetMaterialIdx's material (no flag: the face's own colour OWN; with StippleHighlight the
   lowest flag's list, its first pass = the own material with that bit stripped - Selected left = select_color; without it material 1
   auto_highlight_color / 2 select_color of the lowest flag), then the stipple list's further passes over the same triangles
   (DrawTriangles loops PrepTriangleState per pass): flat glColor4ubv, blended (bit 4: GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, the
   alpha != 0xff), depth-tested / written at the same polygon offset; the scene's depth function is GL_LEQUAL (GrOGLPipe::Draw
   0x10e88147), so the equal depths pass:
     list 0: {0,0,0,0x40, ff, 0}, {ahl rgb, 0x80, 0, 0};   list 1: {0,0,0,0xc4, 0, 0} */
static void gaf_face_draw(int first, int count, int look, int own, float alpha) {
    int fl = look & 3, st = look & 4;
    int lit = !fl ? own : !st ? scene_hl_colors[fl & 1 ? 0 : 1] : fl & 2 ? scene_hl_colors[1] : own;
    float c[3]; mc_col(lit, c); glUniform4f(um_color, c[0], c[1], c[2], alpha);
    glDrawArrays(GL_TRIANGLES, first, count);
    if (!fl || !st) return;
    glDepthFunc(GL_LEQUAL); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glUniform1i(um_flat, 1);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);   /* the view image keeps alpha 1 (the shot / widget paints it with its alpha; a
                                                          window shows only RGB) */
    if (fl & 1) {                          /* list 0 */
        float h[3]; mc_col(scene_hl_colors[0], h);
        glUniform4f(um_color, 0, 0, 0, 0x40 / 255.0f);
        glDrawArrays(GL_TRIANGLES, first, count);
        glUniform1i(um_stip, 1); glUniform4f(um_color, h[0], h[1], h[2], 0x80 / 255.0f);
    } else {                               /* list 1 */
        glUniform1i(um_stip, 1); glUniform4f(um_color, 0, 0, 0, 0xc4 / 255.0f);
    }
    glDrawArrays(GL_TRIANGLES, first, count);
    glUniform1i(um_stip, 0); glUniform1i(um_flat, 0); glDisable(GL_BLEND); glDepthFunc(GL_LESS); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}
/* a solid's GAF face look (formats/ui.md "Shaded face highlight of solids"): kind 0 = part solid si's face f, kind 1 = body si of the
   solids made here (f -1); the flags core_ui gives (scene_sld_hl: bit 0 AutoHighlighted, bit 1 Selected - a body's flag is on every
   face, GrSolid::SetAltRender 0x10e72370 passing it to each GrFace) + StippleHighlight; only the opaque shaded display as surfaces */
static int scene_sld_look(int kind, int si, int f) {
    if (solid_mode != 1 || !scene_sld_hl) return 0;
    int hl = scene_sld_hl(kind, si, f) & 3;
    return hl ? hl | (scene_hl_stipple ? 4 : 0) : 0;
}
typedef struct { int first, n, look; } HlRange;
static int hlr_cmp(const void *a, const void *b) { return ((const HlRange *)a)->first - ((const HlRange *)b)->first; }
/* the solids' mesh (mvao bound, the mesh program on, the own colour set): the unflagged triangles first, then the flagged faces with
   their passes (the draw items: user mask 8 = faces without a highlight flag, then 0x10 | 0x20 = the flagged ones stippled,
   FUN_1801c11d0 / Draw(GrFace) 0x10e8a950). The own colour of a face: this program's solid colour (config 5052, as the base draw) */
static void draw_solid_mesh(void) {
    HlRange *r = NULL; int nr = 0, cap = 0;
    if (solid_mode == 1 && scene_sld_hl) {
        for (int si = 0; si < solid_nmeshed && si < F.nsolids && solid_nf; si++)
            for (int f = 0; f < solid_nf[si]; f++) {
                int k = solid_f0[si] + f; if (!sf_n[k]) continue;
                int look = scene_sld_look(0, si, f); if (!look) continue;
                if (nr == cap) { cap = cap ? cap * 2 : 16; HlRange *q = realloc(r, sizeof *r * (size_t)cap); if (!q) break; r = q; }
                r[nr].first = sf_first[k]; r[nr].n = sf_n[k]; r[nr].look = look; nr++;
            }
        for (int b = 0, t = solid_ntri; b < ubody_cnt; t += ubody_n[b], b++) {
            int look = ubody_n[b] ? scene_sld_look(1, b, -1) : 0; if (!look || t + ubody_n[b] > solid_ntri + user_ntri) continue;
            if (nr == cap) { cap = cap ? cap * 2 : 16; HlRange *q = realloc(r, sizeof *r * (size_t)cap); if (!q) break; r = q; }
            r[nr].first = t; r[nr].n = ubody_n[b]; r[nr].look = look; nr++;
        }
    }
    if (!nr) { glDrawArrays(GL_TRIANGLES, 0, mesh_verts); free(r); return; }
    qsort(r, (size_t)nr, sizeof *r, hlr_cmp);
    int at = 0;
    for (int i = 0; i < nr; i++) { if (r[i].first > at) glDrawArrays(GL_TRIANGLES, at * 3, (r[i].first - at) * 3); if (r[i].first + r[i].n > at) at = r[i].first + r[i].n; }
    if (at * 3 < mesh_verts) glDrawArrays(GL_TRIANGLES, at * 3, mesh_verts - at * 3);
    for (int i = 0; i < nr; i++) gaf_face_draw(r[i].first * 3, r[i].n * 3, r[i].look, cfg_solid_color, 1.0f);
    free(r);
}
/* how surface i's face is drawn highlighted (S-16, formats/ui.md "Shaded vs Outline Shaded"): bit 0 = the GrRender's AutoHighlighted
   flag, bit 1 = its Selected flag, bit 2 = StippleHighlight (config 4053); 0 = not flagged. The flags the frame shows are the repaint's
   (both shaded modes alike): FUN_1801dbc80 -> FUN_18031f5c0 -> gl_Shade 0x1802252e0 -> FUN_1802298b0 -> MaintainGrRenderSurface
   0x180322560 with Selected = the entity's selected bit and the colour find_color(own; the selected bit stripped with StippleHighlight)
   = the entity's own colour, AutoHighlighted on the hovered node's render (MaintainGrRenderSurface, and FUN_1801dbc80 after
   FUN_1801bec50). The GAF scene is drawn only after that, inside FUN_1801dbc80. MaintainGrRenderSurface's SetVisibility index (render_cfg
   4: 1; 3: selected ? 2 : 0) picks one of the render's four prop sets (AddGrPropSets 0x18031da90 / SetGrRenderColor 0x180328970), which
   differ only in the line colour / width; the face's UtRender is the same in all four and a surface's GrFace has no edge data, so no
   pixel changes. Only the opaque shaded display; the translucent one (GrOGLPipe's alpha scaling, this[0x2f8]) is not ported. */
int scene_srf_look(int i) {
    if (solid_mode != 1 || !scene_ent_hl || !surf_shown(i)) return 0;
    int e = F.surfs[i].ent; if (e < 0 || e >= F.nents) return 0;
    int hl = scene_ent_hl(e) & 3;
    return hl ? hl | (scene_hl_stipple ? 4 : 0) : 0;
}
static int surf_shown(int i) { return i >= 0 && i < F.nsurfs && !F.surfs[i].blanked && !F.surfs[i].off && mcam_level_visible(&F, F.surfs[i].level); }   /* Surf.off: deleted / hidden
                                                       here; a hidden level: FUN_18021e020's LevelData::isVisible (mcam_load.h) */
static void mesh_surfs(void) {
    free(surf_tri); free(surf_seg); free(surf_tfirst); free(surf_tn); free(surf_sfirst); free(surf_sn);
    surf_tri = surf_seg = NULL; surf_ntri = surf_nseg = surf_nmeshed = surf_nskip = surf_ctri = 0; surf_tfirst = surf_tn = surf_sfirst = surf_sn = NULL; surf_nwired = 0; surf_up = 1; surf_wire_mode = -1;
    if (!F.nsurfs) return;
    surf_tfirst = calloc(F.nsurfs, sizeof(int)); surf_tn = calloc(F.nsurfs, sizeof(int));
    if (!surf_tfirst || !surf_tn) { free(surf_tfirst); free(surf_tn); surf_tfirst = surf_tn = NULL; return; }
    surf_nmeshed = F.nsurfs;
    /* the shading tolerance gl_tolerance: config 7004 "Chord height?" (ReadCfgFile -> mccore FUN_1801ba530 case 0x1b5c; defaults
       mastercam.dll FUN_1801b4070 0.025 mm / 0.001 inch, decomp/mastercam_gltol.c): McSysTol; the tessellator is OCCT's (ours) */
    double defl = mc_systol_units(mcam_is_metric(&F))->gl_tolerance;
    for (int i = 0; i < F.nsurfs; i++) mesh_one_surf(i, defl);
    if (surf_ntri) {        /* the surfaces in the model extents */
        double a[3] = { 1e300, 1e300, 1e300 }, b[3] = { -1e300, -1e300, -1e300 };
        for (int v = 0; v < surf_ntri * 3; v++) for (int k = 0; k < 3; k++) { double q = surf_tri[v * 6 + k]; if (q < a[k]) a[k] = q; if (q > b[k]) b[k] = q; }
        int had = F.npts > 0 || solid_ntri > 0;
        for (int k = 0; k < 3; k++) { if (!had || a[k] < (&F.lo.x)[k]) (&F.lo.x)[k] = a[k]; if (!had || b[k] > (&F.hi.x)[k]) (&F.hi.x)[k] = b[k]; }
    }
}
static void upload_surf_tris(void) {
    if (!svao) return;                               /* upload() makes the buffers */
    float *sv = malloc((size_t)(surf_ntri ? surf_ntri : 1) * 18 * sizeof(float));
    for (int i = 0; i < surf_ntri * 3; i++) { memcpy(sv + i * 6, surf_tri + i * 6, 24); sv[i * 6] -= ctrx; sv[i * 6 + 1] -= ctry; sv[i * 6 + 2] -= ctrz; }
    glBindVertexArray(svao); glBindBuffer(GL_ARRAY_BUFFER, svbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)surf_ntri * 72, sv, GL_STATIC_DRAW); free(sv);
    glBindVertexArray(0); surf_up = 0; surf_wire_mode = -1;
}
/* the session changed the surfaces (core_ui: new surfaces, a delete / hide / undo, an edited record): new surfaces are meshed, full = 1
   meshes every surface again (an edited record); the wireframe is rebuilt at the next draw. Needs the GL context. */
static int mesh_new_surfs(void) {                    /* 1 when surfaces were added since the last mesh (meshed now) */
    if (F.nsurfs <= surf_nmeshed) return 0;
    if (!surf_tfirst) { mesh_surfs(); return 1; }
    surf_tfirst = realloc(surf_tfirst, sizeof(int) * F.nsurfs); surf_tn = realloc(surf_tn, sizeof(int) * F.nsurfs);
    double defl = mc_systol_units(mcam_is_metric(&F))->gl_tolerance;
    int t0 = surf_ntri;
    for (int i = surf_nmeshed; i < F.nsurfs; i++) mesh_one_surf(i, defl);
    surf_nmeshed = F.nsurfs;
    for (int v = t0 * 3; v < surf_ntri * 3; v++) for (int k = 0; k < 3; k++) {   /* the new surfaces in the model extents (fit) */
        double q = surf_tri[v * 6 + k]; if (q < (&F.lo.x)[k]) (&F.lo.x)[k] = q; if (q > (&F.hi.x)[k]) (&F.hi.x)[k] = q; }
    return 1;
}
void scene_surfs_update(int full) {
    pm_dirty = 1;                                    /* the meshes (deleted / hidden / attributes) follow the same updates */
    if (full || F.nsurfs < surf_nmeshed) { mesh_surfs(); upload_surf_tris(); return; }
    if (mesh_new_surfs()) upload_surf_tris();
    surf_wire_mode = -1;
}
/* surface i's wireframe as drawn (x y z r g b per vertex, 2 vertices per segment, model units): the highlight / pick geometry */
int scene_surf_wire(int i, const float **seg) {
    if (i < 0 || i >= F.nsurfs || !surf_sfirst || i >= surf_nwired) { *seg = NULL; return 0; }
    *seg = surf_seg + (size_t)surf_sfirst[i] * 12; return surf_sn[i];
}
void scene_surf_wire_build(void) { build_surf_wire(); }

/* polygonal meshes (E_MESH, F.meshes; formats/cad_import.md 7): mastercam.dll FUN_180216f30 (draw_ent of a PMESH_ID): the triangles of
   MCLoadSTLMesh in the entity's colour (FUN_180221010; the per-run colours only for STOCK_ID), the record's matrix (+0x78) multiplied in
   (glMultMatrixd: column major, the translation at 12..14); shading off: glPolygonMode(GL_FRONT_AND_BACK, GL_LINE) = the triangles'
   outlines; shading on: filled, each facet with its stored normal (glNormal3fv). Here: the outlines as GL_LINES (x y z r g b), the faces
   as the surfaces' (x y z nx ny nz, the normal through the matrix's rotation); lighting / translucency as the surfaces' (ours). */
static float *pm_tri, *pm_seg; static int pm_ntri, *pm_first, *pm_n, pm_nm, pm_dirty = 1;   /* (declared above upload) */
static GLuint pmvao, pmvbo, pmevao, pmevbo; static int pm_up = 1;
static int pm_ent(int i) { int e = F.meshes[i].ent; return e >= 0 && e < F.nents && F.ents[e].type == E_MESH ? e : -1; }
static int pm_shown(int i) { int e = i >= 0 && i < F.nmeshes ? pm_ent(i) : -1; return e >= 0 && !F.meshes[i].off && !F.ents[e].blanked && mcam_level_visible(&F, F.ents[e].level); }
static void pm_build(void) {
    free(pm_tri); free(pm_seg); free(pm_first); free(pm_n); pm_tri = pm_seg = NULL; pm_first = pm_n = NULL; pm_ntri = 0; pm_nm = F.nmeshes; pm_dirty = 0; pm_up = 1;
    if (!F.nmeshes) return;
    pm_first = calloc(F.nmeshes, sizeof(int)); pm_n = calloc(F.nmeshes, sizeof(int));
    int tot = 0; for (int i = 0; i < F.nmeshes; i++) if (F.meshes[i].kind == 0) tot += F.meshes[i].np;
    pm_tri = malloc((size_t)(tot ? tot : 1) * 18 * sizeof(float)); pm_seg = malloc((size_t)(tot ? tot : 1) * 36 * sizeof(float));
    for (int i = 0; i < F.nmeshes; i++) {
        const McMesh *m = &F.meshes[i]; pm_first[i] = pm_ntri;
        if (m->kind != 0 || !m->p) continue;               /* IFF / MCO data: NOT DECODED here, not drawn */
        double M[16]; memcpy(M, m->rec + 0x78, sizeof M);
        int e = pm_ent(i); float c[3]; mc_col(e >= 0 ? F.ents[e].color : m->color, c);
        for (int k = 0; k < m->np; k++) {
            const StlPoly *q = &m->p[k];
            double n[3] = { M[0] * q->n[0] + M[4] * q->n[1] + M[8] * q->n[2], M[1] * q->n[0] + M[5] * q->n[1] + M[9] * q->n[2], M[2] * q->n[0] + M[6] * q->n[1] + M[10] * q->n[2] };
            float *o = pm_tri + (size_t)pm_ntri * 18, w[9];
            for (int v = 0; v < 3; v++) {
                const float *x = q->v + 3 * v;
                for (int r = 0; r < 3; r++) w[3 * v + r] = (float)(M[r] * x[0] + M[4 + r] * x[1] + M[8 + r] * x[2] + M[12 + r]);
                o[6 * v] = w[3 * v]; o[6 * v + 1] = w[3 * v + 1]; o[6 * v + 2] = w[3 * v + 2];
                o[6 * v + 3] = (float)n[0]; o[6 * v + 4] = (float)n[1]; o[6 * v + 5] = (float)n[2];
            }
            float *g = pm_seg + (size_t)pm_ntri * 36;
            for (int s2 = 0; s2 < 3; s2++) for (int h = 0; h < 2; h++) {
                const float *x = w + 3 * ((s2 + h) % 3); float *d = g + 12 * s2 + 6 * h;
                d[0] = x[0]; d[1] = x[1]; d[2] = x[2]; d[3] = c[0]; d[4] = c[1]; d[5] = c[2];
            }
            pm_ntri++;
        }
        pm_n[i] = pm_ntri - pm_first[i];
    }
    if (pm_ntri) {        /* the meshes in the model extents (as the surfaces, mesh_surfs) */
        double a[3] = { 1e300, 1e300, 1e300 }, b[3] = { -1e300, -1e300, -1e300 };
        for (int v = 0; v < pm_ntri * 3; v++) for (int k = 0; k < 3; k++) { double q = pm_tri[v * 6 + k]; if (q < a[k]) a[k] = q; if (q > b[k]) b[k] = q; }
        int had = F.npts > 0 || solid_ntri > 0 || surf_ntri > 0;
        for (int k = 0; k < 3; k++) { if (!had || a[k] < (&F.lo.x)[k]) (&F.lo.x)[k] = a[k]; if (!had || b[k] > (&F.hi.x)[k]) (&F.hi.x)[k] = b[k]; }
    }
}
static void pm_upload(void) {
    if (pm_dirty || pm_nm != F.nmeshes) pm_build();
    if (!pm_up) return;
    if (!pmvao) {
        glGenVertexArrays(1, &pmvao); glGenBuffers(1, &pmvbo); glBindVertexArray(pmvao); glBindBuffer(GL_ARRAY_BUFFER, pmvbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
        glGenVertexArrays(1, &pmevao); glGenBuffers(1, &pmevbo); glBindVertexArray(pmevao); glBindBuffer(GL_ARRAY_BUFFER, pmevbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    }
    size_t nv = (size_t)(pm_ntri ? pm_ntri : 1) * 3;
    float *b = malloc(nv * 2 * 6 * sizeof(float));
    for (int i = 0; i < pm_ntri * 3; i++) { memcpy(b + i * 6, pm_tri + i * 6, 24); b[i * 6] -= ctrx; b[i * 6 + 1] -= ctry; b[i * 6 + 2] -= ctrz; }
    glBindVertexArray(pmvao); glBindBuffer(GL_ARRAY_BUFFER, pmvbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)pm_ntri * 72, b, GL_STATIC_DRAW);
    for (int i = 0; i < pm_ntri * 6; i++) { memcpy(b + i * 6, pm_seg + i * 6, 24); b[i * 6] -= ctrx; b[i * 6 + 1] -= ctry; b[i * 6 + 2] -= ctrz; }
    glBindVertexArray(pmevao); glBindBuffer(GL_ARRAY_BUFFER, pmevbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)pm_ntri * 144, b, GL_STATIC_DRAW);
    free(b); glBindVertexArray(0); pm_up = 0;
}
/* mesh i's triangles (18 floats each: x y z nx ny nz, model units) / outline segments (12 floats each), 0 = not drawn: the pick geometry */
int scene_mesh_tris(int i, const float **tri) {
    if (pm_dirty || pm_nm != F.nmeshes) pm_build();
    if (i < 0 || i >= pm_nm || !pm_first || !pm_shown(i)) { *tri = NULL; return 0; }
    *tri = pm_tri + (size_t)pm_first[i] * 18; return pm_n[i];
}
int scene_mesh_wire(int i, const float **seg) {
    if (pm_dirty || pm_nm != F.nmeshes) pm_build();
    if (i < 0 || i >= pm_nm || !pm_first || !pm_shown(i)) { *seg = NULL; return 0; }
    *seg = pm_seg + (size_t)pm_first[i] * 36; return pm_n[i] * 3;
}
void scene_meshes_changed(void) { pm_dirty = 1; }
/* solid i's faces: fn[f] triangles each, face after face from solid_ntri on (called before its triangles are appended) */
static void solid_face_ranges(int i, const int *fn, int nfc) {
    if (!solid_f0 || !solid_nf) return;
    solid_f0[i] = sf_tot; solid_nf[i] = 0;
    if (nfc <= 0 || !fn) return;
    int *a = realloc(sf_first, sizeof(int) * (size_t)(sf_tot + nfc)), *b = a ? realloc(sf_n, sizeof(int) * (size_t)(sf_tot + nfc)) : NULL;
    if (a) sf_first = a;
    if (!a || !b) return;
    sf_n = b;
    int t = solid_ntri;
    for (int f = 0; f < nfc; f++) { sf_first[sf_tot + f] = t; sf_n[sf_tot + f] = fn[f]; t += fn[f]; }
    solid_nf[i] = nfc; sf_tot += nfc;
}
static void mesh_solids(void) {
    free(solid_tri); solid_tri = NULL; solid_ntri = solid_faces = solid_meshed = solids_ok = 0; solid_msg[0] = 0;
    free(solid_f0); free(solid_nf); free(sf_first); free(sf_n); sf_first = sf_n = NULL; sf_tot = 0;
    solid_f0 = calloc(F.nsolids + 1, sizeof(int)); solid_nf = calloc(F.nsolids + 1, sizeof(int));
    free(solid_tfirst); free(solid_tn); solid_tfirst = calloc(F.nsolids + 1, sizeof(int)); solid_tn = calloc(F.nsolids + 1, sizeof(int));
    solid_nmeshed = solid_tfirst && solid_tn ? F.nsolids : 0;
    if (!solid_nmeshed) { free(solid_tfirst); free(solid_tn); solid_tfirst = solid_tn = NULL; }
    int nfail = 0; char err[128] = "";
    for (int i = 0; i < F.nsolids; i++) {
        if (solid_tfirst) solid_tfirst[i] = solid_ntri;
        if (F.solids[i].brep) {        /* an imported solid (STEP): OCCT's mesh, model units -> metres like the XT's (solid_scale below) */
            float *t = NULL; char e2[128] = ""; int *fn = NULL, nfc = 0;
            int nt = occt_brep_mesh_faces(F.solids[i].brep, F.solids[i].brep_n, mcam_is_metric(&F) ? 0.02 : 0.001, &t, &fn, &nfc, e2, sizeof e2);
            if (nt < 0) { snprintf(err, sizeof err, "%s", e2); nfail++; free(fn); continue; }
            solid_face_ranges(i, fn, nfc); free(fn);
            double u = mcam_is_metric(&F) ? 0.001 : 0.0254;      /* metres per model unit (the XT convention of solid_tri) */
            for (int q = 0; q < nt * 3; q++) { t[q * 6] *= (float)u; t[q * 6 + 1] *= (float)u; t[q * 6 + 2] *= (float)u; }
            solids_ok++; solid_tri = realloc(solid_tri, (size_t)(solid_ntri + nt) * 18 * sizeof(float));
            memcpy(solid_tri + (size_t)solid_ntri * 18, t, (size_t)nt * 18 * sizeof(float));
            solid_ntri += nt; if (solid_tn) solid_tn[i] = nt; free(t);
            continue;
        }
        XtSchema *sc = find_schema(F.solids[i].xt, F.solids[i].xt_size);
        if (!sc) { snprintf(err, sizeof err, "Parasolid schema file not found (set MCAM_SCHEMA_DIR)"); nfail++; continue; }
        XtMesh m;
        if (!xt_mesh(sc, F.solids[i].xt, F.solids[i].xt_size, &m)) { snprintf(err, sizeof err, "%s", m.error); nfail++; continue; }
        solid_face_ranges(i, m.fntri, m.fntri ? m.nfaces : 0);
        solids_ok++; solid_faces += m.nfaces; solid_meshed += m.nfaces_meshed;
        solid_tri = realloc(solid_tri, (size_t)(solid_ntri + m.ntri) * 18 * sizeof(float));
        memcpy(solid_tri + (size_t)solid_ntri * 18, m.tri, (size_t)m.ntri * 18 * sizeof(float));
        solid_ntri += m.ntri; if (solid_tn) solid_tn[i] = m.ntri;
        xt_mesh_free(&m);
    }
    /* XT is in metres; choose mm (x1000) or inch (x39.37) by matching the part's other extents */
    double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
    for (int v = 0; v < solid_ntri * 3; v++) for (int k = 0; k < 3; k++) {
        double q = solid_tri[v * 6 + k]; if (q < lo[k]) lo[k] = q; if (q > hi[k]) hi[k] = q; }
    solid_scale = 1000;
    int anybrep = 0; for (int i = 0; i < F.nsolids; i++) if (F.solids[i].brep) anybrep = 1;
    if (anybrep) solid_scale = mcam_is_metric(&F) ? 1000 : 1000 / 25.4;   /* an imported solid: its units are the part's */
    else if (solid_ntri && F.npts) {
        double best = 1e300; const double cand[2] = { 1000, 1000 / 25.4 };
        for (int c = 0; c < 2; c++) {
            double e = 0;
            for (int k = 0; k < 3; k++) {
                double a = (&F.lo.x)[k], b = (&F.hi.x)[k];
                e += fabs((lo[k] + hi[k]) / 2 * cand[c] - (a + b) / 2) + fabs((hi[k] - lo[k]) * cand[c] - (b - a));
            }
            if (e < best) { best = e; solid_scale = cand[c]; }
        }
    }
    for (int v = 0; v < solid_ntri * 3; v++) for (int k = 0; k < 3; k++) solid_tri[v * 6 + k] *= (float)solid_scale;
    have_stock = xt_stock = solid_ntri > 0;
    for (int k = 0; k < 3; k++) { stock_lo[k] = xt_lo[k] = lo[k] * solid_scale; stock_hi[k] = xt_hi[k] = hi[k] * solid_scale; }
    user_stock();
    if (solid_ntri) {        /* include the solids in the model extents */
        for (int k = 0; k < 3; k++) {
            double a = lo[k] * solid_scale, b = hi[k] * solid_scale;
            if (!F.npts) { (&F.lo.x)[k] = a; (&F.hi.x)[k] = b; }
            else { if (a < (&F.lo.x)[k]) (&F.lo.x)[k] = a; if (b > (&F.hi.x)[k]) (&F.hi.x)[k] = b; }
        }
    }
    snprintf(solid_msg, sizeof solid_msg, "solids: %d/%d meshed, %d/%d faces, %d triangles%s%s", solids_ok, F.nsolids,
             solid_meshed, solid_faces, solid_ntri, nfail ? " - " : "", nfail ? err : "");
}

/* drafting entities (dimensions / notes / labels / leaders, viewer/draft.c - mccore GetDraftWireframe): world lines and filled
   arrowheads in the entity's colour (draw_dimension / draw_note: find_color of the entity), drawn with the geometry. The stroke
   fonts: paths.conf "fonts" (MCAMUI_FONTS), else <config dir>/fonts (tools/setup_from_install.py copies them there) */
static float *dft_v, *dft_t; static int dft_n, dft_cap, dft_tn, dft_tcap;
static short *dft_vw, *dft_tvw; static int dft_iso = 1, dft_view;          /* each vertex's entity view; iso_view_dimensions (config 15007) */
static DraftFonts *dft_fonts; static int dft_fonts_tried;
static void dft_push(float **v, int *n, int *cap, const double *p, const float *c) {
    if (*n == *cap) { *cap = *cap ? 2 * *cap : 4096; *v = realloc(*v, sizeof(float) * 6 * (size_t)*cap);
        if (v == &dft_v) dft_vw = realloc(dft_vw, sizeof(short) * (size_t)*cap); else dft_tvw = realloc(dft_tvw, sizeof(short) * (size_t)*cap); }
    if (v == &dft_v) dft_vw[*n] = (short)dft_view; else dft_tvw[*n] = (short)dft_view;
    float *q = *v + 6 * (size_t)(*n)++; q[0] = (float)p[0]; q[1] = (float)p[1]; q[2] = (float)p[2]; q[3] = c[0]; q[4] = c[1]; q[5] = c[2];
}
static void dft_poly(void *ctx, const double *p, int n, int closed, int fill, int color, int view) {
    (void)ctx; float c[3]; mc_col(color, c); dft_view = view;
    if (fill) { for (int i = 1; i + 1 < n; i++) { dft_push(&dft_t, &dft_tn, &dft_tcap, p, c); dft_push(&dft_t, &dft_tn, &dft_tcap, p + 3 * i, c);
                                                  dft_push(&dft_t, &dft_tn, &dft_tcap, p + 3 * (i + 1), c); } }
    for (int i = 0; i + 1 < n; i++) { dft_push(&dft_v, &dft_n, &dft_cap, p + 3 * i, c); dft_push(&dft_v, &dft_n, &dft_cap, p + 3 * (i + 1), c); }
    if (closed && n > 2) { dft_push(&dft_v, &dft_n, &dft_cap, p + 3 * (n - 1), c); dft_push(&dft_v, &dft_n, &dft_cap, p, c); }
}
char draft_msg[400];
static int dft_n0, dft_tn0;                                  /* the part's own drafting (draft_build); the session's follow (scene_draft_session) */
static char dft_msg0[400];
static const DraftFonts *dft_fonts_get(void) {
    if (!dft_fonts_tried) {
        dft_fonts_tried = 1; char d[1100];
        const char *k = site_path("fonts");
        dft_fonts = draft_fonts_load(k[0] ? k : config_file(d, sizeof d, "fonts"));
    }
    return dft_fonts;
}
static char dft_path[1100];                                   /* ours: the part draft_build read (scene_draft_rebuild) */
static int dft_np, dft_tnp; static DraftStats dft_stp;         /* ours: the part's own records drawn (draft_build; the notes follow) */
static void draft_notes(void) {                               /* the notes (F.notes, E_NOTE) after the part's records, the message */
    dft_n = dft_np; dft_tn = dft_tnp; draft_msg[0] = 0;
    DraftOut o = { NULL, dft_poly }; DraftStats st = dft_stp;
    if (loaded && F.nnotes) {                                    /* drawn from F.notes (E_NOTE: deleted / blanked ones not, Ent colour) */
        DraftDoc *nd = mcam_notes_doc(&F, -1); DraftStats s2;
        draft_render(nd, dft_fonts_get(), &o, &s2); draft_free(nd);
        st.notes += s2.notes; st.leaders += s2.leaders; st.skipped += s2.skipped;
        if (s2.note[0] && strlen(st.note) + strlen(s2.note) + 3 < sizeof st.note) snprintf(st.note + strlen(st.note), sizeof st.note - strlen(st.note), "%s%s", st.note[0] ? "; " : "", s2.note);
    }
    if (st.dims || st.notes || st.leaders || st.hatches || st.skipped) {
        snprintf(draft_msg, sizeof draft_msg, "drafting: %d dimension(s), %d note(s), %d leader(s)%s%d skipped%s%s%s", st.dims, st.notes, st.leaders,
                 st.skipped ? ", " : "", st.skipped, st.note[0] ? " (" : "", st.note, st.note[0] ? ")" : "");
        if (!st.skipped) { char *c = strstr(draft_msg, "0 skipped"); if (c) *c = 0; }
    }
    dft_n0 = dft_n; dft_tn0 = dft_tn; snprintf(dft_msg0, sizeof dft_msg0, "%s", draft_msg);
}
static void draft_build(const char *path) {
    if (path != dft_path) snprintf(dft_path, sizeof dft_path, "%s", path ? path : "");
    dft_n = dft_tn = dft_n0 = dft_tn0 = dft_np = dft_tnp = 0; draft_msg[0] = dft_msg0[0] = 0; memset(&dft_stp, 0, sizeof dft_stp);
    if ((!path || !path[0]) && !(loaded && F.nnotes)) return;
    dft_fonts_get();
    /* draw_dimension / draw_note: drawn when the entity's view is the graphics view, or always with iso_view_dimensions - config
       15007 "entity view equals graphics view?" (mccore config writer FUN_1801d0880(0x3a9f, iso_view_dimensions), decomp/draft/
       core_cfgdraft.c:775; startup value 1, text4.c:199) */
    { char cv[64]; mcam_config_value(mcam_is_metric(&F), 15007, cv, sizeof cv); dft_iso = !(cv[0] == 'n' || cv[0] == 'N'); }
    DraftDoc *dd = path && path[0] ? draft_load(path) : NULL;
    DraftOut o = { NULL, dft_poly };
    if (dd && draft_count(dd)) draft_render(dd, dft_fonts, &o, &dft_stp);   /* the part's notes are skipped here (core_ui draft_set_skip): */
    draft_free(dd);
    dft_np = dft_n; dft_tnp = dft_tn;
    draft_notes();
}
/* the drafting entities made in this session (core_ui cui_draft.c: the stored ones, the live one, the dimension being dragged): N
   records of LEN bytes, drawn after the part's own */
static uint8_t *dft_sess; static int dft_sess_n; static unsigned dft_sess_len;   /* ours: the session's records as last given (a rebuild of
                                                                         the part's drafting draws them again) */
static void draft_session_draw(void) {
    dft_n = dft_n0; dft_tn = dft_tn0; snprintf(draft_msg, sizeof draft_msg, "%s", dft_msg0);
    if (dft_sess_n <= 0) return;
    DraftOut o = { NULL, dft_poly }; DraftStats st;
    draft_render_records(dft_sess, dft_sess_n, dft_sess_len, dft_fonts_get(), &o, &st);
}
void scene_draft_rebuild(void) { draft_build(dft_path); draft_session_draw(); }   /* ours: the part's drafting again (draft_set_skip changed) */
void scene_notes_rebuild(void) { if (!dft_path[0] && !dft_np && !(loaded && F.nnotes)) return; draft_notes(); draft_session_draw(); }   /* ours: the notes
                                                                         again (one deleted / restored / made / moved / recoloured) */
void scene_draft_session(const uint8_t *recs, int n, unsigned len) {
    free(dft_sess); dft_sess = NULL; dft_sess_n = 0;
    if (n > 0) { dft_sess = malloc((size_t)n * len); memcpy(dft_sess, recs, (size_t)n * len); dft_sess_n = n; dft_sess_len = len; }
    draft_session_draw();
}
static void draw_dynamic(const float *v, int n, GLenum mode);
static void draft_draw(float cx, float cy, float cz) {
    int gv = view_number(vmat);                                  /* get_gr_view_n */
    for (int s = 0; s < 2; s++) {
        int n = s ? dft_tn : dft_n; const float *v = s ? dft_t : dft_v; const short *vw = s ? dft_tvw : dft_vw;
        if (n <= 0) continue;
        float *tv = malloc((size_t)n * 24); int m = 0, per = s ? 3 : 2;
        for (int i = 0; i + per <= n; i += per) {
            if (!dft_iso && vw[i] != gv) continue;
            for (int k = 0; k < per; k++) { memcpy(tv + m * 6, v + (i + k) * 6, 24); tv[m * 6] -= cx; tv[m * 6 + 1] -= cy; tv[m * 6 + 2] -= cz; m++; }
        }
        if (m) draw_dynamic(tv, m, s ? GL_TRIANGLES : GL_LINES);
        free(tv);
    }
}

int part_file_units = -1;
void csn_reset(void) __attribute__((weak));   /* mcpartline.c: CSplineNodeMgr::AllocateMem at dbinit / init_db (CS-5) */
int load_file(const char *p) {
    McamFile nf;                                 /* load into a fresh model: a failed open keeps the current part */
    if (!mcam_load(&nf, p)) { snprintf(status, sizeof status, "%s", mcam_error()); return 0; }
    lock_ctr = 0;
    if (loaded && scene_part_closing) scene_part_closing();
    if (loaded) mcam_free(&F);
    F = nf; loaded = 1; scene_part_gen++;
    if (csn_reset) csn_reset();                    /* dbinit -> CSplineNodeMgr::AllocateMem (mccore 0x1802c4770) */
    /* ProcessVersionEntity (mccore FUN_1802d5f60): header units < 2 that differ from IsMetric() -> ChangeConfiguration(units):
       the system units follow the part (formats/units.md 2); unknown / mixed (-1 / 2) keep the current ones */
    if (F.metric == 0 || F.metric == 1) mc_units_metric = F.metric;
    mc_systol_units(mc_units_metric);              /* ChangeConfiguration's ReadCfgFile: the tolerances of that configuration (formats/units.md 8) */
    part_file_units = F.metric;
    sel = -1; sim = -1; playing = 0;
    keep_view = 0;
    scene_set_overlay(NULL, 0);
    scene_set_dash(NULL, 0);
    mesh_solids(); mesh_surfs(); pm_dirty = 1;
    upload();
    free(dft_sess); dft_sess = NULL; dft_sess_n = 0;   /* another document: the session's drafting is the new one's (dr_scene) */
    draft_build(p);
    snprintf(status, sizeof status, "loaded %s", path_sep_r(p) ? path_sep_r(p) + 1 : p);
    snprintf(path_buf, sizeof path_buf, "%s", p);
    if (scene_part_changed) scene_part_changed();
    return 1;
}

void new_file(void) {                     /* an empty part (File > New), ready for geometry */
    if (loaded && scene_part_closing) scene_part_closing();
    if (loaded) mcam_free(&F);
    mcam_new_empty(&F); lock_ctr = 0; scene_part_gen++;
    if (csn_reset) csn_reset();           /* init_db -> CSplineNodeMgr::AllocateMem (mastercam.dll 0x18033ae30) */
    F.metric = mc_units_metric;           /* init_db keeps the system units: the new part is IsMetric() (formats/units.md 3) */
    part_file_units = -1;
    loaded = 1; sel = -1; sim = -1; playing = 0; keep_view = 0;
    scene_set_overlay(NULL, 0);
    scene_set_dash(NULL, 0);
    mesh_solids(); mesh_surfs(); pm_dirty = 1;
    upload();
    free(dft_sess); dft_sess = NULL; dft_sess_n = 0;
    draft_build(NULL);
    snprintf(status, sizeof status, "new part");
    path_buf[0] = 0;
    if (scene_part_changed) scene_part_changed();
}

void unload_file(void) {
    if (loaded && scene_part_closing) scene_part_closing();
    if (loaded) mcam_free(&F);
    memset(&F, 0, sizeof F); scene_part_gen++;
    F.lo.x = F.lo.y = F.lo.z = -50; F.hi.x = F.hi.y = F.hi.z = 50;
    loaded = 0; sel = -1; sim = -1; playing = 0; keep_view = 0;
    scene_set_overlay(NULL, 0);
    scene_set_dash(NULL, 0);
    mesh_solids(); mesh_surfs(); pm_dirty = 1;
    upload();
    free(dft_sess); dft_sess = NULL; dft_sess_n = 0;
    draft_build(NULL);
    status[0] = 0;
    if (scene_part_changed) scene_part_changed();
}

long tp_total(void) {          /* toolpath points in the current selection */
    if (sel >= 0) return F.ops[sel].npts;
    long n = 0; for (int i = 0; i < F.nops; i++) n += F.ops[i].npts; return n;
}

/* ------------------------------------------------------------------------------------------------ draw */
static M4 camera(int w, int h) {
    /* parallel projection of Mastercam's view: view = V * (p - ctr) + V * ctr; the screen shows view x in
       vcx +- vht/2 * aspect and view y in vcy +- vht/2 */
    float asp = (float)w / (float)(h > 0 ? h : 1);
    const double *V = vmat;
    double c0x = V[0] * ctrx + V[1] * ctry + V[2] * ctrz, c0y = V[3] * ctrx + V[4] * ctry + V[5] * ctrz, c0z = V[6] * ctrx + V[7] * ctry + V[8] * ctrz;
    M4 view = { { (float)V[0], (float)V[3], (float)V[6], 0, (float)V[1], (float)V[4], (float)V[7], 0, (float)V[2], (float)V[5], (float)V[8], 0,
                  (float)(c0x - vcx), (float)(c0y - vcy), (float)c0z, 1 } };
    float hh = (float)(vht * 0.5), R = (float)(ext * 4 + vht * 4 + fabs(c0z) * 2 + 10);
    M4 proj = m4_ortho(-hh * asp, hh * asp, -hh, hh, -R, R);
    return m4_mul(proj, view);
}

/* large arrays go to GL in chunks (ours): a driver copies each buffer it is given and some crash instead of failing when that
   copy cannot be allocated (seen: libnvidia-eglcore under ulimit -v with a 5M-triangle stock). GL_LINES / GL_TRIANGLES split
   on whole primitives; strips overlap one vertex. */
#define DYN_CHUNK (1 << 20)
/* room for the driver's copy of an upload of BYTES plus a margin (ours): the driver crashes instead of failing (above) */
static int gl_headroom(size_t bytes) {
    size_t need = bytes > SIZE_MAX / 4 - (64u << 20) ? SIZE_MAX : bytes * 3 + (64u << 20);
    void *p = need == SIZE_MAX ? NULL : malloc(need); if (!p) return 0; free(p); return 1;
}
int scene_gl_skipped;                               /* a draw was cut short for lack of memory (the overlay reports it) */
static void draw_dynamic(const float *v, int n, GLenum mode) {
    glBindVertexArray(dyn_vao); glBindBuffer(GL_ARRAY_BUFFER, dyn_vbo);
    int per = mode == GL_TRIANGLES ? 3 : mode == GL_LINES ? 2 : 1, step = DYN_CHUNK - DYN_CHUNK % (per * 2 > 2 ? per : 2);
    int strip = mode == GL_LINE_STRIP || mode == GL_TRIANGLE_STRIP;
    if (n <= DYN_CHUNK && n < 65536) { glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * 24, v, GL_STREAM_DRAW); glDrawArrays(mode, 0, n); return; }
    for (int at = 0; at < n; at += strip ? step - 1 : step) {
        int c = n - at < step ? n - at : step; if (strip && c < 2) break;
        if (!gl_headroom((size_t)c * 24)) { scene_gl_skipped = 1; return; }   /* the rest is not drawn (ours) */
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)c * 24, v + (size_t)at * 6, GL_STREAM_DRAW); glDrawArrays(mode, 0, c);
    }
}

/* ---------------------------------------------------------------------------------------- Backplot tool (13.4) */
/* GL state of backplot.dll FUN_66d78a00 / FUN_66d35aa0 as a shader: GL_COLOR_MATERIAL (AMBIENT_AND_DIFFUSE) = the
   vertex colour; material specular (1,1,1,1), shininess 5.0 (FUN_66d31460 default); headlight GL_LIGHT6 colour 0xEAEAEA:
   diffuse 234/255, specular c + (1 - c) / 1.25, ambient = specular x 0.05, directional from the viewer. GL's default
   light-model ambient (0.2) is kept - Mastercam's value and the state of lights 0-5 are NOT DECODED. One-sided lighting
   (LIGHT_MODEL_TWO_SIDE not set in the decoded code). Alpha = the colour's alpha (opacity). */
static GLuint bp_tprog; static GLint ut_mvp, ut_light, ut_color;
static void bp_tool_mesh_add(float **v, int *n, int *cap, const double *r, const double *z, int np, double z0,
                             float tx, float ty, float tz, int segs, double spin) {
    for (int k = 1; k < np; k++) {
        double dr = r[k] - r[k - 1], dz = z[k] - z[k - 1], l = hypot(dr, dz);
        if (l < 1e-12) continue;
        double nr = dz / l, nz = -dr / l;                      /* outward normal in the (r, z) plane (outline from the tip up) */
        for (int i = 0; i < segs; i++) {
            double a0 = spin + 2 * M_PI * i / segs, a1 = spin + 2 * M_PI * (i + 1) / segs;
            double P[4][3] = { { r[k - 1] * cos(a0), r[k - 1] * sin(a0), z[k - 1] }, { r[k - 1] * cos(a1), r[k - 1] * sin(a1), z[k - 1] },
                               { r[k] * cos(a1), r[k] * sin(a1), z[k] }, { r[k] * cos(a0), r[k] * sin(a0), z[k] } };
            double am = (a0 + a1) / 2, N[3] = { nr * cos(am), nr * sin(am), nz };
            static const int T[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
            for (int t = 0; t < 2; t++) {
                const double *A = P[T[t][0]], *B = P[T[t][1]], *C = P[T[t][2]];
                double u[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, w[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
                double c[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
                double d = c[0] * N[0] + c[1] * N[1] + c[2] * N[2];
                if (fabs(d) < 1e-18) continue;                   /* degenerate (on the axis) */
                const double *Q[3] = { A, d > 0 ? B : C, d > 0 ? C : B };   /* counter-clockwise seen from outside */
                if (*n + 3 > *cap) { *cap = *cap ? *cap * 2 : 4096; *v = realloc(*v, (size_t)*cap * 24); }
                for (int j = 0; j < 3; j++) { float *q = *v + (size_t)(*n)++ * 6;
                    q[0] = tx + (float)Q[j][0]; q[1] = ty + (float)Q[j][1]; q[2] = tz + (float)(Q[j][2] + z0);
                    q[3] = (float)N[0]; q[4] = (float)N[1]; q[5] = (float)N[2]; }
            }
        }
    }
}
static void bp_draw_tool(const M4 *mvp, float lx, float ly, float lz) {
    const Tool *tl = mcam_tool(&F, bp_tool_num);
    /* backplotbnci FUN_67d1a2e0 builds the tool from the NCI tool attributes the op carries (tool_type, tool_dia, ...),
       not from the part's tool list; when the list has no such tool the op's own tool data is used (bp_tool_op, set by
       mcamui). Its no-data branch (type 10, length 1.0, flute 0.99, holder D * 1.1 x 0.25) runs only with the caller
       flag +0x19 (forced tool vector), which plain backplot does not set: not used. No data at all = no tool drawn. */
    if (!tl) { if (bp_tool_op.dia <= 0) return; tl = &bp_tool_op; }
    Tool cut = *tl; cut.holder_n = 0; cut.holder_rz = NULL;    /* the cutter (entity+8 list) and the holder (entity+0x18) apart */
    double orr[96], ozz[96]; int on = tool_outline(&cut, orr, ozz, 96);
    double hr[64], hz[64]; int hn = 0;
    if (tl->holder_n && tl->holder_rz) {                       /* stacked as in tool_outline: face at the tool length */
        double top = tl->oal > 0 ? tl->oal : (on ? ozz[on - 1] : 0);
        if (tl->holder_rz[0] > 1e-12) { hr[hn] = 0; hz[hn++] = top; }
        for (int i = 0; i < tl->holder_n && hn < 63; i++) { hr[hn] = tl->holder_rz[2 * i]; hz[hn++] = top + tl->holder_rz[2 * i + 1]; }
        if (hr[hn - 1] > 1e-12) { hr[hn] = 0; hz[hn] = hz[hn - 1]; hn++; }
    }
    float tx = (float)(bp_tool_pos[0] - ctrx), ty = (float)(bp_tool_pos[1] - ctry), tz = (float)(bp_tool_pos[2] - ctrz);
    double spin = bp_spin_deg * M_PI / 180;                     /* rotation about the tool axis (0,0,1) */
    const int SEGS = 48;                                       /* tessellation NOT DECODED: the earlier 48-step rings */
    for (int part = 0; part < 2; part++) {
        if (part == 0 ? !bp_draw_cutter : !bp_draw_holder || !hn) continue;
        float *v = NULL; int n = 0, cap = 0;
        if (part == 0) bp_tool_mesh_add(&v, &n, &cap, orr, ozz, on, 0, tx, ty, tz, SEGS, spin);
        else bp_tool_mesh_add(&v, &n, &cap, hr, hz, hn, 0, tx, ty, tz, SEGS, spin);
        if (!n) { free(v); continue; }
        const float *c = part == 0 ? bp_tool_rgba : bp_holder_rgba;
        if (bp_tool_shaded) {
            if (!bp_tprog) {
                const char *vs = "#version 330 core\nlayout(location=0) in vec3 pos; layout(location=1) in vec3 nrm;\n"
                                 "uniform mat4 mvp; out vec3 n; void main(){ n = nrm; gl_Position = mvp * vec4(pos,1); }\n";
                const char *fs = "#version 330 core\nin vec3 n; uniform vec3 light; uniform vec4 color; out vec4 o;\n"
                                 "void main(){ float c = 234.0/255.0, sp = c + (1.0 - c) / 1.25; vec3 N = normalize(n);\n"
                                 " float d = max(dot(N, light), 0.0), s = d > 0.0 ? pow(max(dot(N, light), 0.0), 5.0) : 0.0;\n"
                                 " vec3 rgb = color.rgb * (0.2 + sp * 0.05) + color.rgb * c * d + vec3(sp * s);\n"
                                 " o = vec4(min(rgb, vec3(1.0)), color.a); }\n";
                bp_tprog = glCreateProgram();
                glAttachShader(bp_tprog, shader(GL_VERTEX_SHADER, vs)); glAttachShader(bp_tprog, shader(GL_FRAGMENT_SHADER, fs));
                glLinkProgram(bp_tprog);
                ut_mvp = glGetUniformLocation(bp_tprog, "mvp"); ut_light = glGetUniformLocation(bp_tprog, "light"); ut_color = glGetUniformLocation(bp_tprog, "color");
            }
            glUseProgram(bp_tprog);
            glUniformMatrix4fv(ut_mvp, 1, GL_FALSE, mvp->m); glUniform3f(ut_light, lx, ly, lz); glUniform4f(ut_color, c[0], c[1], c[2], c[3]);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_CULL_FACE);
            glFrontFace(GL_CCW);
            glBindVertexArray(dyn_vao); glBindBuffer(GL_ARRAY_BUFFER, dyn_vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * 24, v, GL_STREAM_DRAW);
            glCullFace(GL_FRONT); glDrawArrays(GL_TRIANGLES, 0, n);  /* pass 1: back faces */
            glCullFace(GL_BACK); glDrawArrays(GL_TRIANGLES, 0, n);   /* pass 2: front faces */
            glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
            glUseProgram(prog);
        } else {                                                 /* Plain / Fluted: unlit, glPolygonMode(GL_LINE), one pass */
            for (int i = 0; i < n; i++) { v[i * 6 + 3] = c[0]; v[i * 6 + 4] = c[1]; v[i * 6 + 5] = c[2]; }
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            draw_dynamic(v, n, GL_TRIANGLES);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        free(v);
    }
}
/* point entities: mastercam.dll draw_3d_point_style @1803301c0 (decomp/mastercam_pointstyle.c), line style solid,
   width 1, in the entity colour; u = the view height (+0x760 = vht). Style 1 (and any other value) = draw_3d_point: three
   lines along the world X / Y / Z axes, +-u * 0.01388889 (1/72). Styles 3-6 use d = u * 0.01 in the screen plane
   (FUN_180236dc0: point + V^T * offset): 3 plus (+-d horizontal / vertical), 4 X (diagonals +-d), 5 circle of radius d
   (21 chords of 18 degrees from (d, 0)), 6 square of side 2d. Style 2 = FUN_18021b550 (a bare GL point; its size,
   FUN_18021b780, NOT DECODED: 1 px). 7 / 8 = the same point + draw_thread_point_no_off / draw_cut_point_no_off (sketcher
   only; their glyphs NOT DECODED: the point alone). */
static void draw_point_markers(void) {
    int np = 0; for (int i = 0; i < F.nents; i++) if (F.ents[i].type == E_POINT) np++;
    if (!np) return;
    float *buf = malloc((size_t)np * 42 * 2 * 6 * sizeof(float)), *pts = malloc((size_t)np * 6 * sizeof(float));
    int n = 0, nd = 0;
    double u = vht, s1 = u * 0.01388889, d = u * 0.01;
    const double *R = vmat, *Up = vmat + 3;                    /* screen right / up in world coordinates */
#define PV(X, Y, Z) do { float *q = buf + n * 6; q[0] = (float)((X) - ctrx); q[1] = (float)((Y) - ctry); q[2] = (float)((Z) - ctrz); q[3] = c[0]; q[4] = c[1]; q[5] = c[2]; n++; } while (0)
#define SV(DX, DY) PV(p.x + R[0] * (DX) + Up[0] * (DY), p.y + R[1] * (DX) + Up[1] * (DY), p.z + R[2] * (DX) + Up[2] * (DY))
    for (int i = 0; i < F.nents; i++) {
        const Ent *e = &F.ents[i]; if (e->type != E_POINT || e->blanked || !mcam_level_visible(&F, e->level)) continue;   /* draw_ent: not blanked, a visible level */
        float c[3]; mc_col(e->first_poly >= 0 && e->first_poly < F.npolys ? F.polys[e->first_poly].color : e->color, c);
        V3 p = e->a; int st = e->pt_style;
        switch (st) {
        case 2: case 7: case 8: { float *q = pts + nd * 6; q[0] = (float)(p.x - ctrx); q[1] = (float)(p.y - ctry); q[2] = (float)(p.z - ctrz); q[3] = c[0]; q[4] = c[1]; q[5] = c[2]; nd++; } break;
        case 3: SV(-d, 0); SV(d, 0); SV(0, -d); SV(0, d); break;
        case 4: SV(d, d); SV(-d, -d); SV(-d, d); SV(d, -d); break;
        case 5: { double a = 0; for (int k = 0; k < 21; k++) { SV(d * cos(a), d * sin(a)); a += 0.3141592653589792; SV(d * cos(a), d * sin(a)); } } break;
        case 6: SV(-d, d); SV(d, d); SV(d, d); SV(d, -d); SV(d, -d); SV(-d, -d); SV(-d, -d); SV(-d, d); break;
        default: PV(p.x + s1, p.y, p.z); PV(p.x - s1, p.y, p.z); PV(p.x, p.y + s1, p.z); PV(p.x, p.y - s1, p.z);
                 PV(p.x, p.y, p.z + s1); PV(p.x, p.y, p.z - s1); break;
        }
    }
#undef SV
#undef PV
    if (n) draw_dynamic(buf, n, GL_LINES);
    if (nd) draw_dynamic(pts, nd, GL_POINTS);
    free(buf); free(pts);
}
static void bp_draw_endpoints(void) {                        /* 13.3: RenderStyle {2.0, 0xFFFFFF, type 0} -> glPointSize 2, white */
    if (!bp_ep || bp_nep <= 0) return;
    float *v = malloc((size_t)bp_nep * 24);
    for (int i = 0; i < bp_nep; i++) { v[i * 6] = bp_ep[i * 3] - ctrx; v[i * 6 + 1] = bp_ep[i * 3 + 1] - ctry; v[i * 6 + 2] = bp_ep[i * 3 + 2] - ctrz;
                                       v[i * 6 + 3] = v[i * 6 + 4] = v[i * 6 + 5] = 1; }
    glPointSize(2.0f); draw_dynamic(v, bp_nep, GL_POINTS); glPointSize(1.0f);
    free(v);
}

/* entity line styles that draw stippled (set_gl_style, mastercam.dll 0x180226bc0: 2-7 set a GL_LINE_STIPPLE pattern
   except 5, which is 0xffff = solid; 0xf8-0xfe are the table's inverse-pattern specials, never a real entity style);
   their polylines are left out of the static draw and drawn from the dash buffer instead (core profile has no
   GL_LINE_STIPPLE, so mcamui bakes the dash segments in screen px) */
static int dashed_style(int s) { s &= 0xff; return (s >= 2 && s <= 7 && s != 5) || (s >= 0xf8 && s <= 0xfe); }

void draw_scene(int x, int y, int w, int h) {
    if (scene_draw_hook) scene_draw_hook();
    glViewport(x, y, w, h);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glUseProgram(prog);
    if (theme) {                               /* gradient background (Mastercam config 5007 start / 5009 end, see mcamui) */
        static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
#define BOT bg_bot[0], bg_bot[1], bg_bot[2]
#define TOP bg_top[0], bg_top[1], bg_top[2]
        float bg[6 * 6] = { -1, -1, 0, BOT, 1, -1, 0, BOT, 1, 1, 0, TOP,
                            -1, -1, 0, BOT, 1, 1, 0, TOP, -1, 1, 0, TOP };
#undef BOT
#undef TOP
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, I);
        glDisable(GL_DEPTH_TEST);
        draw_dynamic(bg, 6, GL_TRIANGLES);
        glEnable(GL_DEPTH_TEST);
    }
    M4 mvp = camera(w, h);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp.m);
    static float buf[6 * 8192]; int n = 0;
#define VTX(X, Y, Z, R, G, B) do { float *q = buf + n * 6; q[0] = X; q[1] = Y; q[2] = Z; q[3] = R; q[4] = G; q[5] = B; n++; } while (0)
    if (show_grid && grid.style == 0 && grid.dx > 1e-9) {
        /* infinite fading grid under the part (its lowest Z), so the part stands on it. The lines are the grid's X/Y
           positions exactly (spacing from the grid origin, Top plane): the same points snapping uses. */
        float gz = (float)grid_z() - ctrz;
        float inv[16], ex = 0, ey = 0, ez = (float)vht, tx = 0, ty = 0;
        if (m4_inv(mvp.m, inv)) {
            float pp[2][3];                               /* screen-centre ray: near point (eye side) and far point */
            for (int k = 0; k < 2; k++) { float v[4] = { 0, 0, k ? 1.0f : -1.0f, 1 }, r[4];
                for (int i = 0; i < 4; i++) r[i] = inv[i] * v[0] + inv[4 + i] * v[1] + inv[8 + i] * v[2] + inv[12 + i] * v[3];
                for (int i = 0; i < 3; i++) pp[k][i] = r[i] / r[3]; }
            ex = pp[0][0]; ey = pp[0][1]; ez = pp[0][2];
            float dz = pp[1][2] - pp[0][2], t = fabsf(dz) > 1e-9f ? (gz - pp[0][2]) / dz : -1;
            if (t > 0 && t < 1) { tx = pp[0][0] + t * (pp[1][0] - pp[0][0]); ty = pp[0][1] + t * (pp[1][1] - pp[0][1]); }
            else { tx = ex; ty = ey; }
        }
        /* fade radius from the eye: generous, so the grid reaches the horizon (perspective) or covers the view (ortho) */
        float fadeR = ortho ? 0.0f : (float)vht * 8 + ext * 2, R = ortho ? (float)vht * 4 : fadeR * 1.1f;
        float cx = ortho ? tx : ex, cy = ortho ? ty : ey;
        float q[6 * 6] = { cx - R, cy - R, gz, 0, 0, 0,  cx + R, cy - R, gz, 0, 0, 0,  cx + R, cy + R, gz, 0, 0, 0,
                           cx - R, cy - R, gz, 0, 0, 0,  cx + R, cy + R, gz, 0, 0, 0,  cx - R, cy + R, gz, 0, 0, 0 };
        glUseProgram(gprog);
        glUniformMatrix4fv(ug_mvp, 1, GL_FALSE, mvp.m); glUniform3f(ug_ctr, ctrx, ctry, ctrz);
        glUniform1f(ug_base, (float)grid.dx); glUniform2f(ug_org, (float)grid.ox, (float)grid.oy);
        glUniform3f(ug_target, ex + ctrx, ey + ctry, ez + ctrz); glUniform1f(ug_fade, fadeR);
        if (theme) glUniform3f(ug_col, .22f, .26f, .34f); else glUniform3f(ug_col, .55f, .58f, .65f);
        glUniform1i(ug_axes, 1);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(2, 2);   /* faces lying exactly at Z 0 win */
        draw_dynamic(q, 6, GL_TRIANGLES);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glUseProgram(prog);
    }
    if (show_grid && grid.style == 1 && grid.dx > 1e-9 && grid.dy > 1e-9) {
        /* Mastercam's display_sel_grid: a point at every grid node, size/2 each way from the origin, in the grid colour;
           plus dashed lines on the outline and through the origin. Construction plane Top, depth 0. */
        float gc[3]; mc_col(grid.color, gc);                  /* grid colour (config 5032) through the palette */
        int nx = (int)(grid.size * 0.5 / grid.dx), ny = (int)(grid.size * 0.5 / grid.dy);
        float gz = -ctrz, ox = (float)(grid.ox - ctrx), oy = (float)(grid.oy - ctry);
        float hx = (float)(nx * grid.dx), hy = (float)(ny * grid.dy);
        long npts = (long)(2 * nx + 1) * (2 * ny + 1);
        if (npts <= 400000) {                            /* the nodes (very dense grids are skipped, as they would fill the screen) */
            float *pv = malloc((size_t)npts * 24); long k = 0;
            for (int j = -ny; j <= ny; j++) for (int i = -nx; i <= nx; i++) {
                float *q = pv + k * 6; q[0] = ox + (float)(i * grid.dx); q[1] = oy + (float)(j * grid.dy); q[2] = gz;
                q[3] = gc[0]; q[4] = gc[1]; q[5] = gc[2]; k++; }
            glPointSize(2.5f);
            draw_dynamic(pv, (int)k, GL_POINTS);
            glPointSize(1.0f);
            free(pv);
        }
        n = 0;                                           /* dashed outline + cross through the origin */
        float seg[6][4] = { { -hx, -hy, hx, -hy }, { -hx, hy, hx, hy }, { -hx, -hy, -hx, hy }, { hx, -hy, hx, hy }, { -hx, 0, hx, 0 }, { 0, -hy, 0, hy } };
        float dash = (float)fmax(grid.dx, grid.dy) * 0.5f;
        for (int l = 0; l < 6 && n < 8000; l++) {
            float L = hypotf(seg[l][2] - seg[l][0], seg[l][3] - seg[l][1]); int nd = (int)(L / dash);
            if (nd > 1800) nd = 1800;
            for (int d = 0; d < nd; d += 2) {
                float t0 = (float)d / nd, t1 = (float)(d + 1) / nd;
                VTX(ox + seg[l][0] + (seg[l][2] - seg[l][0]) * t0, oy + seg[l][1] + (seg[l][3] - seg[l][1]) * t0, gz, gc[0], gc[1], gc[2]);
                VTX(ox + seg[l][0] + (seg[l][2] - seg[l][0]) * t1, oy + seg[l][1] + (seg[l][3] - seg[l][1]) * t1, gz, gc[0], gc[1], gc[2]);
            }
        }
        glLineWidth(1.0f);
        draw_dynamic(buf, n, GL_LINES);
        n = 0;
    }
    if (show_info_axes) {   /* F9: X / Y / Z lines through the system origin, across the whole view */
        float L = ((float)vht + ext) * 200.0f;
        const float *c = origin_col;
        VTX(-ctrx - L, -ctry, -ctrz, c[0], c[1], c[2]); VTX(-ctrx + L, -ctry, -ctrz, c[0], c[1], c[2]);
        VTX(-ctrx, -ctry - L, -ctrz, c[0], c[1], c[2]); VTX(-ctrx, -ctry + L, -ctrz, c[0], c[1], c[2]);
        VTX(-ctrx, -ctry, -ctrz - L, c[0], c[1], c[2]); VTX(-ctrx, -ctry, -ctrz + L, c[0], c[1], c[2]);
        glLineWidth(1.0f);
        draw_dynamic(buf, n, GL_LINES);
        n = 0;
    }
    if (show_axes) {   /* world axes at the system origin */
        float s = ext * 0.15f;
        VTX(-ctrx, -ctry, -ctrz, .9f, .1f, .1f); VTX(-ctrx + s, -ctry, -ctrz, .9f, .1f, .1f);
        VTX(-ctrx, -ctry, -ctrz, 0, .6f, 0); VTX(-ctrx, -ctry + s, -ctrz, 0, .6f, 0);
        VTX(-ctrx, -ctry, -ctrz, .1f, .25f, .95f); VTX(-ctrx, -ctry, -ctrz + s, .1f, .25f, .95f);
        glLineWidth(2.5f);
        draw_dynamic(buf, n, GL_LINES);
        glLineWidth(1.0f);
        n = 0;
        float gzz = (float)grid_z();
        if (show_grid && gzz < -1e-6f) {                 /* dashed drop line from the origin to the grid under the part */
            int nd = 24;
            for (int i = 0; i < nd; i += 2) { float z0 = gzz * i / nd, z1 = gzz * (i + 1) / nd;
                VTX(-ctrx, -ctry, z0 - ctrz, .35f, .35f, .45f); VTX(-ctrx, -ctry, z1 - ctrz, .35f, .35f, .45f); }
            draw_dynamic(buf, n, GL_LINES);
            n = 0;
        }
    }
    if (!loaded) return;
    /* static geometry + toolpaths via glMultiDrawArrays over the visible polylines */
    int m = 0;
    long budget = sim >= 0 ? sim : LONG_MAX;
    V3 toolpos = { 0 }; int have_tool = 0, curtool = 0;
    for (int i = 0; i < F.npolys && i < capgfirst; i++) {   /* capgfirst: polys added since the last upload have no GPU offset yet */
        const Poly *p = &F.polys[i];
        if (p->kind == K_GEOM) { if (show_geom && p->count > 0 && GEOM_SHOWN(p) && !dashed_style(p->ln_style) && p->width <= 1) { firsts[m] = gfirst[i]; counts[m++] = p->count; } continue; }   /* count 0 = deleted (undo ent_off): an all-deleted part left an empty buffer that the GL driver crashed on */
        if (!show_tp || bp_lines_on) break;                 /* Backplot draws its own motions (below) */
        if (sel >= 0 && p->op != F.ops[sel].id) continue;
        if (scene_tp_hide_id && p->op == scene_tp_hide_id) continue;   /* Q20: the op dialog's preview is drawn instead (core_ui cui_preview.c) */
        if (budget <= 0) break;
        int c = p->count < budget ? p->count : (int)budget;
        budget -= p->count;
        if (p->kind == K_RAPID && !show_rapid) continue;
        firsts[m] = gfirst[i]; counts[m++] = c;
        if (c > 0) { have_tool = 1; curtool = p->tool; toolpos = F.pts[p->first + c - 1]; }
    }
    float cxl = (float)vmat[6], cyl = (float)vmat[7], czl = (float)vmat[8];   /* headlight: towards the viewer */
    if ((solid_mode == 1 || (solid_mode == 0 && scene_force_mesh)) && mesh_verts) {
        glUseProgram(mprog);
        glUniformMatrix4fv(um_mvp, 1, GL_FALSE, mvp.m); glUniform3f(um_light, cxl, cyl, czl);
        glUniform1f(um_tintz, 0);
        { float sc[3]; mc_col(cfg_solid_color, sc); glUniform4f(um_color, sc[0], sc[1], sc[2], 1); }   /* config 5052 */
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1, 1);
        glBindVertexArray(mvao); draw_solid_mesh();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glUseProgram(prog);
    }
    if (surf_ntri && show_geom && solid_mode != 0) {   /* the surfaces' faces, each in its entity colour (see mesh_surfs) */
        glUseProgram(mprog);
        glUniformMatrix4fv(um_mvp, 1, GL_FALSE, mvp.m); glUniform3f(um_light, cxl, cyl, czl); glUniform1f(um_tintz, 0);
        int ghost = solid_mode == 2;
        if (ghost) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
        else { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1, 0); }   /* a unit nearer than the solids' (1, 1): a surface lying on a
                                                                               solid face is drawn over it, not z-fighting (ours) */
        glBindVertexArray(svao);
        /* the GAF face highlight (scene_srf_look): the draw sequences (FUN_1801c11d0: render_cfg 3 DAT_18099fec8 = user masks 0xc,
           0x34; render_cfg 4 DAT_18099fea8 = 0xc, 0xa, 0x34, 0x32) and Draw(GrFace) (0x10e8a950) skips a face whose highlight flags
           (vtable +0x188, & 3) are set when the mask has bit 8 and an unflagged one when it has bit 0x10: the flagged (auto-highlighted
           or selected) faces are drawn after every other face, in the item with 0x20 = the stipple allowed. The passes (InitStippleData
           FUN_1801c8440, GetStippleIdx 0x10e926d0 = the lowest flag bit -> material 1 auto-highlight / 2 select, its stipple list):
           - StippleHighlight, AutoHighlighted (with or without Selected): list 0 = lit in the face's own material with the flag bit
             stripped (GetMaterialIdx 0x10e927c0: Selected left -> material 2 = select_color, else the render's own UtRender = the
             entity's colour), flat black alpha 0x40, flat auto_highlight_color alpha 0x80 through mask 0;
           - StippleHighlight, Selected only: list 1 = lit in select_color, flat black alpha 0xc4 through mask 0;
           - no StippleHighlight: no list, the face lit in material 1 (auto_highlight_color) / 2 (select_color) of the lowest flag. */
        for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < surf_nmeshed; i++) if (surf_tn[i] && surf_shown(i)) {
            int look = scene_srf_look(i);
            if (((look & 3) != 0) != (pass == 1)) continue;
            gaf_face_draw(surf_tfirst[i] * 3, surf_tn[i] * 3, look, surf_color(i), ghost ? 0.38f : 1.0f);
        }
        if (ghost) { glDepthMask(GL_TRUE); glDisable(GL_BLEND); } else glDisable(GL_POLYGON_OFFSET_FILL);
        glUseProgram(prog);
    }
    if (F.nsurfs && show_geom) { build_surf_wire(); upload_surf_wire(); }
    if (surf_nseg && show_geom) {                  /* their wireframe (flowlines / outline) */
        glBindVertexArray(sevao); glDrawArrays(GL_LINES, 0, surf_nseg * 2);
    }
    if (F.nmeshes && show_geom) {                  /* polygonal meshes (FUN_180216f30): faces when shaded, triangle outlines otherwise */
        pm_upload();
        if (pm_ntri && solid_mode != 0) {
            glUseProgram(mprog);
            glUniformMatrix4fv(um_mvp, 1, GL_FALSE, mvp.m); glUniform3f(um_light, cxl, cyl, czl); glUniform1f(um_tintz, 0);
            int ghost = solid_mode == 2;
            if (ghost) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
            else { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1, 0); }
            glBindVertexArray(pmvao);
            for (int i = 0; i < pm_nm; i++) if (pm_n[i] && pm_shown(i)) {
                float c[3]; mc_col(F.ents[pm_ent(i)].color, c); glUniform4f(um_color, c[0], c[1], c[2], ghost ? 0.38f : 1.0f);
                glDrawArrays(GL_TRIANGLES, pm_first[i] * 3, pm_n[i] * 3);
            }
            if (ghost) { glDepthMask(GL_TRUE); glDisable(GL_BLEND); } else glDisable(GL_POLYGON_OFFSET_FILL);
            glUseProgram(prog);
        } else if (pm_ntri) {
            glBindVertexArray(pmevao);
            for (int i = 0; i < pm_nm; i++) if (pm_n[i] && pm_shown(i)) glDrawArrays(GL_LINES, pm_first[i] * 6, pm_n[i] * 6);
        }
    }
    if (vf_n) {                                   /* the verify stock mesh (lit like the solids, depth-tested) */
        glUseProgram(mprog);
        glUniformMatrix4fv(um_mvp, 1, GL_FALSE, mvp.m); glUniform3f(um_light, cxl, cyl, czl);
        glUniform4f(um_color, vf_rgb[0], vf_rgb[1], vf_rgb[2], 1);
        glUniform1f(um_tintz, 1); glUniform2f(um_zrange, (float)(vf_z[0] - ctrz), (float)(vf_z[1] - ctrz));   /* depth tint: ours */
        glBindVertexArray(vfao); glDrawArrays(GL_TRIANGLES, 0, vf_n);
        glUseProgram(prog);
    }
    if (user_nedge && scene_render_cfg() != 3) {  /* Shaded (render_cfg 3): no edges - its GAF draw sequence DAT_18099fec8 (FUN_1801c11d0)
                                                     has only the triangle items (user masks 0xc / 0x34), render_cfg 4's DAT_18099fea8
                                                     adds the line items 0xa / 0x32; sld_draw_solid's translucent outline (0xc9) is for
                                                     _render_cfg 4 only */
                                                  /* the solids' edges: sld_draw_solids draws every solid with _render_cfg 1
                                                     (sld_draw_solid_wire FUN_1800b46b0: the solid's colour, lighting off,
                                                     depth-tested) in every display mode; the faces above are offset */
        /* colour: wireframe (_render_cfg < 3) = the solid's colour (sld_draw_solid 0x1800b3eb1: ent +6); shaded = the
           outline pass of _render_cfg 4 (the RENDER_CFG default mode, sld_draw_solids 0x1800b4aa0 tail: lighting off,
           sld_in_outlined_drawing -> 0x1800b3ea8 colour RENDER_CFG +0x12 = config 8027); translucent = 0xc9
           (sld_draw_solid 0x1800b3e57, the outline drawn with the translucent faces). Width: nothing in this path
           calls set_gl_width (sld_draw_solid, FUN_1800b46b0, set_gl_wireframe mastercam 0x180227020), so GL's 1 px. */
        float sc[3]; mc_col(solid_mode == 0 ? cfg_solid_color : solid_mode == 2 ? 0xc9 : cfg_outline_color, sc);
        float *tv = malloc((size_t)user_nedge * 2 * 24);
        for (int i = 0; i < user_nedge * 2; i++) {
            tv[i * 6] = user_edge[i * 3] - (float)ctrx; tv[i * 6 + 1] = user_edge[i * 3 + 1] - (float)ctry; tv[i * 6 + 2] = user_edge[i * 3 + 2] - (float)ctrz;
            tv[i * 6 + 3] = sc[0]; tv[i * 6 + 4] = sc[1]; tv[i * 6 + 5] = sc[2];
        }
        glLineWidth(1.0f);
        draw_dynamic(tv, user_nedge * 2, GL_LINES); free(tv);
        glLineWidth(1.0f);
    }
    glBindVertexArray(vao);
    glLineWidth(theme ? 1.8f : 1.4f);
    if (m) glMultiDrawArrays(GL_LINE_STRIP, firsts, counts, m);
    if (show_geom) {                                 /* wider entities (ent +0x10, set_gl_width: the overload's own scaling NOT DECODED -
                                                        ours: this program's base width + (width - 1) px) */
        float base = theme ? 1.8f : 1.4f;
        for (int i = 0; i < F.npolys && i < capgfirst; i++) { const Poly *p = &F.polys[i];
            if (p->kind != K_GEOM) break;
            if (p->width > 1 && p->count > 0 && GEOM_SHOWN(p) && !dashed_style(p->ln_style)) { glLineWidth(base + p->width - 1); glDrawArrays(GL_LINE_STRIP, gfirst[i], p->count); } }
        glLineWidth(base);
    }
    if (ndash && show_geom) {                        /* non-solid line styles, baked screen-space by core_ui (build_overlay) */
        if (dash_ctr[0] != ctrx || dash_ctr[1] != ctry || dash_ctr[2] != ctrz) {   /* the model centre moved: shift in place */
            for (int i = 0; i < ndash; i++) { dashv[i * 6] += dash_ctr[0] - ctrx; dashv[i * 6 + 1] += dash_ctr[1] - ctry; dashv[i * 6 + 2] += dash_ctr[2] - ctrz; }
            dash_ctr[0] = ctrx; dash_ctr[1] = ctry; dash_ctr[2] = ctrz;
        }
        draw_dynamic(dashv, ndash, GL_LINES);
    }
    glLineWidth(1.0f);
    if (show_geom) draft_draw(ctrx, ctry, ctrz);       /* drafting entities */
    if (show_geom) draw_point_markers();
    if (bp_lines_on) {                               /* Backplot motions (formats/backplot.md 12.3) */
        glLineWidth(theme ? 1.8f : 1.4f);
        for (int s = 0; s < 2; s++) if (bp_ln[s] > 0) {
            float *tv = malloc((size_t)bp_ln[s] * 24);
            for (int i = 0; i < bp_ln[s]; i++) { memcpy(tv + i * 6, bp_lv[s] + i * 6, 24); tv[i * 6] -= ctrx; tv[i * 6 + 1] -= ctry; tv[i * 6 + 2] -= ctrz; }
            draw_dynamic(tv, bp_ln[s], GL_LINES); free(tv);
        }
        glLineWidth(1.0f);
        bp_draw_endpoints();
    }
    if (solid_mode == 2 && mesh_verts) {            /* ghost: translucent, no depth writes, drawn last */
        glUseProgram(mprog);
        glUniformMatrix4fv(um_mvp, 1, GL_FALSE, mvp.m); glUniform3f(um_light, cxl, cyl, czl);
        glUniform1f(um_tintz, 0);
        { float sc[3]; mc_col(cfg_solid_color, sc); glUniform4f(um_color, sc[0], sc[1], sc[2], 0.38f); }   /* alpha: NOT DECODED (Mastercam translucency level unknown) */
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        glBindVertexArray(mvao); glDrawArrays(GL_TRIANGLES, 0, mesh_verts);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glUseProgram(prog);
    }
    if (stk_n) {                                     /* the shaded stock model (FUN_18033fe70): translucent, no depth writes */
        if (!skvao) {
            glGenVertexArrays(1, &skvao); glGenBuffers(1, &skvbo);
            glBindVertexArray(skvao); glBindBuffer(GL_ARRAY_BUFFER, skvbo);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
        }
        if (stk_ctr[0] != ctrx || stk_ctr[1] != ctry || stk_ctr[2] != ctrz) {   /* the model centre moved: shift in place */
            for (int i = 0; i < stk_n; i++) { stk_v[i * 6] += stk_ctr[0] - ctrx; stk_v[i * 6 + 1] += stk_ctr[1] - ctry; stk_v[i * 6 + 2] += stk_ctr[2] - ctrz; }
            stk_ctr[0] = ctrx; stk_ctr[1] = ctry; stk_ctr[2] = ctrz; stk_up = 1;
        }
        if (stk_up && !gl_headroom((size_t)stk_n * 24)) { free(stk_v); stk_v = NULL; stk_n = 0; stk_up = 0; scene_gl_skipped = 1; }   /* ours, see draw_dynamic */
        if (stk_up) {                                    /* the storage first, the data in chunks (see draw_dynamic) */
            glBindBuffer(GL_ARRAY_BUFFER, skvbo); glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)stk_n * 24, NULL, GL_STATIC_DRAW);
            for (int at = 0; at < stk_n; at += DYN_CHUNK) { int c = stk_n - at < DYN_CHUNK ? stk_n - at : DYN_CHUNK;
                if (!gl_headroom((size_t)c * 24)) { scene_gl_skipped = 1; free(stk_v); stk_v = NULL; stk_n = 0; break; }
                glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)at * 24, (GLsizeiptr)c * 24, stk_v + (size_t)at * 6); }
            if (glGetError() == GL_OUT_OF_MEMORY) { free(stk_v); stk_v = NULL; stk_n = 0; }   /* GL could not take it: not drawn (ours) */
            stk_up = 0;
        }
        if (stk_n) {
        glUseProgram(mprog);
        glUniformMatrix4fv(um_mvp, 1, GL_FALSE, mvp.m); glUniform3f(um_light, cxl, cyl, czl); glUniform1f(um_tintz, 0);
        glUniform4f(um_color, stk_rgba[0], stk_rgba[1], stk_rgba[2], stk_rgba[3]);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        glBindVertexArray(skvao); glDrawArrays(GL_TRIANGLES, 0, stk_n);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glUseProgram(prog);
        }
    }
    if (bp_tool_on) bp_draw_tool(&mvp, cxl, cyl, czl);       /* Backplot: tool / holder (formats/backplot.md 13.4) */
    else if (show_tool && sim >= 0 && have_tool) {   /* tool: its side outline (cutter + holder) revolved; outline math: tool_profiles.md */
        const Tool *tl = mcam_tool(&F, curtool);
        /* backplotbnci FUN_67d1a2e0 builds the tool from the NCI tool attributes the op carries (tool_type, tool_dia, ...),
       not from the part's tool list; when the list has no such tool the op's own tool data is used (bp_tool_op, set by
       mcamui). Its no-data branch (type 10, length 1.0, flute 0.99, holder D * 1.1 x 0.25) runs only with the caller
       flag +0x19 (forced tool vector), which plain backplot does not set: not used. No data at all = no tool drawn. */
    if (!tl) { if (bp_tool_op.dia <= 0) return; tl = &bp_tool_op; }
        double orr[96], ozz[96]; int on = tool_outline(tl, orr, ozz, 96);
        float tx = (float)(toolpos.x - ctrx), ty = (float)(toolpos.y - ctry), tz = (float)(toolpos.z - ctrz);
        float tc = theme ? .12f : 1.0f;                   /* dark tool on the light theme */
        n = 0;
        for (int i = 0; i < 48; i++) {                    /* rings at each outline vertex */
            float a0 = i * (float)M_PI / 24, a1 = (i + 1) * (float)M_PI / 24;
            for (int k = 0; k < on; k++) {
                if (orr[k] < 1e-6) continue;
                float r = (float)orr[k], z = tz + (float)ozz[k];
                VTX(tx + r * cosf(a0), ty + r * sinf(a0), z, tc, tc, tc + .05f);
                VTX(tx + r * cosf(a1), ty + r * sinf(a1), z, tc, tc, tc + .05f);
            }
        }
        for (int i = 0; i < 8; i++) {                     /* 8 meridians along the outline */
            float a = i * (float)M_PI / 4;
            for (int k = 1; k < on; k++) {
                VTX(tx + (float)orr[k - 1] * cosf(a), ty + (float)orr[k - 1] * sinf(a), tz + (float)ozz[k - 1], tc, tc, tc + .05f);
                VTX(tx + (float)orr[k] * cosf(a), ty + (float)orr[k] * sinf(a), tz + (float)ozz[k], tc, tc, tc + .05f);
            }
        }
        glLineWidth(1.5f);
        draw_dynamic(buf, n, GL_LINES);
        glLineWidth(1.0f);
    }
    if (novl) {                                          /* selection / chaining overlay on top of everything */
        float *tv = malloc((size_t)novl * 24);
        if (tv) for (int i = 0; i < novl; i++) { memcpy(tv + i * 6, ovl + i * 6, 24); tv[i * 6] -= ctrx; tv[i * 6 + 1] -= ctry; tv[i * 6 + 2] -= ctrz; }
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.5f);
        draw_dynamic(tv, novl, GL_LINES);
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
        free(tv);
    }
    if (gnt_n || gnl_n) {                                /* the Dynamic gnomon over everything (its own depth: GetAxesDrawOrder's
                                                           painter order replaced, ours); lighting = the mesh shader's (ours: render
                                                           type 8 NOT DECODED) */
        if (!gn_prog) {
            const char *vs = "#version 330 core\nlayout(location=0) in vec3 pos; layout(location=1) in vec3 nrm; layout(location=2) in vec4 col;\n"
                             "uniform mat4 mvp; out vec3 n; out vec4 c; void main(){ n = nrm; c = col; gl_Position = mvp * vec4(pos,1); }\n";
            const char *fs = "#version 330 core\nin vec3 n; in vec4 c; uniform vec3 light; out vec4 o;\n"
                             "void main(){ float d = abs(dot(normalize(n), light)); o = vec4(c.rgb * (0.30 + 0.70 * d), c.a); }\n";
            gn_prog = glCreateProgram();
            glAttachShader(gn_prog, shader(GL_VERTEX_SHADER, vs)); glAttachShader(gn_prog, shader(GL_FRAGMENT_SHADER, fs));
            glLinkProgram(gn_prog);
            ugn_mvp = glGetUniformLocation(gn_prog, "mvp"); ugn_light = glGetUniformLocation(gn_prog, "light");
            glGenVertexArrays(1, &gn_vao); glGenBuffers(1, &gn_vbo);
            glBindVertexArray(gn_vao); glBindBuffer(GL_ARRAY_BUFFER, gn_vbo);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, (void *)0);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, (void *)12);
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 40, (void *)24);
        }
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        if (gnt_n) {
            float *tv = malloc((size_t)gnt_n * 40);
            if (tv) {
                memcpy(tv, gnt_v, (size_t)gnt_n * 40);
                for (int i = 0; i < gnt_n; i++) { tv[i * 10] -= ctrx; tv[i * 10 + 1] -= ctry; tv[i * 10 + 2] -= ctrz; }
                glUseProgram(gn_prog);
                glUniformMatrix4fv(ugn_mvp, 1, GL_FALSE, mvp.m); glUniform3f(ugn_light, cxl, cyl, czl);
                glBindVertexArray(gn_vao); glBindBuffer(GL_ARRAY_BUFFER, gn_vbo);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)gnt_n * 40, tv, GL_STREAM_DRAW);
                /* opaque parts first, then the translucent ones (alpha < 1) without depth writes: one pass each over the
                   buffer, the other kind discarded by a run split (ours) */
                glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                for (int pass = 0; pass < 2; pass++) {
                    glDepthMask(pass == 0 ? GL_TRUE : GL_FALSE);
                    for (int i = 0; i < gnt_n; ) {
                        int op = tv[i * 10 + 9] >= 0.999f, j = i;
                        while (j < gnt_n && (tv[j * 10 + 9] >= 0.999f) == op) j++;
                        int a = i - i % 3, b = j - j % 3;   /* whole triangles */
                        if (op == (pass == 0) && b > a) glDrawArrays(GL_TRIANGLES, a, b - a);
                        i = j > i ? j : i + 1;
                    }
                }
                glDepthMask(GL_TRUE); glDisable(GL_BLEND);
                glUseProgram(prog);
                free(tv);
            }
        }
        if (gnl_n) {
            float *lv = malloc((size_t)gnl_n * 24);
            if (lv) {
                for (int i = 0; i < gnl_n; i++) { memcpy(lv + i * 6, gnl_v + i * 6, 24); lv[i * 6] -= ctrx; lv[i * 6 + 1] -= ctry; lv[i * 6 + 2] -= ctrz; }
                glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp.m);
                glLineWidth(1.0f); draw_dynamic(lv, gnl_n, GL_LINES);
                free(lv);
            }
        }
    }
    if (show_axes) {   /* view indicator in the bottom-left corner (orientation only), like Mastercam's */
        int gs = 90;
        glViewport(x + 8, y + 8, gs, gs);
        glClear(GL_DEPTH_BUFFER_BIT);
        M4 v = { { (float)vmat[0], (float)vmat[3], (float)vmat[6], 0, (float)vmat[1], (float)vmat[4], (float)vmat[7], 0,
                   (float)vmat[2], (float)vmat[5], (float)vmat[8], 0, 0, 0, 0, 1 } };
        M4 pr = m4_ortho(-1.4f, 1.4f, -1.4f, 1.4f, -10, 10), mv2 = m4_mul(pr, v);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mv2.m);
        n = 0;
        VTX(0, 0, 0, .9f, .1f, .1f); VTX(1, 0, 0, .9f, .1f, .1f);
        VTX(0, 0, 0, 0, .6f, 0); VTX(0, 1, 0, 0, .6f, 0);
        VTX(0, 0, 0, .1f, .25f, .95f); VTX(0, 0, 1, .1f, .25f, .95f);
        /* letters X, Y, Z as small strokes at the axis ends */
        VTX(1.12f, -.08f, 0, .9f, .1f, .1f); VTX(1.28f, .08f, 0, .9f, .1f, .1f); VTX(1.12f, .08f, 0, .9f, .1f, .1f); VTX(1.28f, -.08f, 0, .9f, .1f, .1f);
        VTX(0, 1.12f, 0, 0, .6f, 0); VTX(0, 1.2f, 0, 0, .6f, 0); VTX(0, 1.2f, 0, 0, .6f, 0); VTX(-.07f, 1.28f, 0, 0, .6f, 0); VTX(0, 1.2f, 0, 0, .6f, 0); VTX(.07f, 1.28f, 0, 0, .6f, 0);
        VTX(-.07f, 0, 1.28f, .1f, .25f, .95f); VTX(.07f, 0, 1.28f, .1f, .25f, .95f); VTX(.07f, 0, 1.28f, .1f, .25f, .95f); VTX(-.07f, 0, 1.12f, .1f, .25f, .95f);
        VTX(-.07f, 0, 1.12f, .1f, .25f, .95f); VTX(.07f, 0, 1.12f, .1f, .25f, .95f);
        glLineWidth(2.0f);
        draw_dynamic(buf, n, GL_LINES);
        glLineWidth(1.0f);
        glViewport(x, y, w, h);
    }
#undef VTX
}

/* ------------------------------------------------------------------------------------------------ view (Mastercam) */
/* reserved view matrices (mastercam.dll ReservedPlanes::BuildStandardMatrices, decomp/mastercam_stdviews.c): rows are
   screen right / up / towards the viewer; 1 Top, 2 Front, 3 Back, 4 Bottom, 5 Right, 6 Left, 7 Isometric, 8 Axonometric */
static const double STD_VIEW[9][9] = {
    { 1, 0, 0, 0, 1, 0, 0, 0, 1 },
    { 1, 0, 0, 0, 1, 0, 0, 0, 1 },
    { 1, 0, 0, 0, 0, 1, 0, -1, 0 },
    { -1, 0, 0, 0, 0, 1, 0, 1, 0 },
    { -1, 0, 0, 0, 1, 0, 0, 0, -1 },
    { 0, 1, 0, 0, 0, 1, 1, 0, 0 },
    { 0, -1, 0, 0, 0, 1, -1, 0, 0 },
    { 0.70710678118655, 0.70710678118655, 0.0, -0.40824829046386, 0.40824829046386, 0.81649658092772,
      0.57735026918962, -0.57735026918962, 0.57735026918962 },
    { 0.5, -0.85355339059, 0.14644660942, 0.5, 0.14644660942, -0.85355339059, 0.70710678118655, 0.5, 0.5 } };

/* fit_toolpath (mastercam.dll export, initial value 1 in .data): Fit adds the toolpath extents (FUN_180320770 runs
   draw_bnci in mode 2 to collect bplot_min / max); the view transition clears it for its target fit */
int fit_toolpath = 1;
int view_w = 800, view_h = 600;                          /* graphics viewport in pixels (set by the caller) */
int fit_anim = 1, anim_delay = 2;                        /* config 33007 Animate zoom fits, 33008 Animation Delay */
double cfg_zoom_fac = 0.0625;                            /* config 4026 zoom factor (mccore zoom_fac) */

void view_get(ViewState *v) { memcpy(v->m, vmat, sizeof vmat); v->cx = vcx; v->cy = vcy; v->h = vht; }
void view_put(const ViewState *v) { memcpy(vmat, v->m, sizeof vmat); vcx = v->cx; vcy = v->cy; vht = v->h; }

/* SetGrView: a new view matrix; the screen centre (view coordinates) and the height stay */
void view_set_matrix(const double m[9]) { memcpy(vmat, m, sizeof vmat); }
void view_std(int n) { if (n >= 1 && n <= 8) view_set_matrix(STD_VIEW[n]); }
int view_std_matrix(int n, double m[9]) { if (n < 1 || n > 8) return 0; memcpy(m, STD_VIEW[n], sizeof STD_VIEW[n]); return 1; }   /* ReservedPlanes::BuildStandardMatrices */

/* FUN_18013dbd0: the mean of the displayed points (display-list vertices, plus sld_get_displayed_midpt - here the
   middle of the solid box) as a view depth; none -> the construction depth (0 here) */
double view_mean_depth(void) {
    double sum[3] = { 0, 0, 0 }; long cnt = 0;              /* FUN_18013dbd0: mean of the display-list points */
    for (int i = 0; i < F.npolys; i++) if (F.polys[i].kind == K_GEOM && GEOM_SHOWN(&F.polys[i]))   /* the display list: visible levels */
        for (int j = 0; j < F.polys[i].count; j++) { V3 q = F.pts[F.polys[i].first + j]; sum[0] += q.x; sum[1] += q.y; sum[2] += q.z; cnt++; }
    if (solid_ntri > 0) {                                   /* + sld_get_displayed_midpt (here: middle of the solid box) */
        double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
        for (int i = 0; i < solid_ntri * 3; i++) for (int k = 0; k < 3; k++) { double t = solid_tri[i * 6 + k]; if (t < lo[k]) lo[k] = t; if (t > hi[k]) hi[k] = t; }
        for (int k = 0; k < 3; k++) sum[k] += (lo[k] + hi[k]) * 0.5; cnt++;
    }
    double z = 0;                                           /* construction depth 0 */
    if (cnt) { double m[3] = { sum[0] / cnt, sum[1] / cnt, sum[2] / cnt }, mv[3]; world_to_view3(m, mv); z = mv[2]; }
    return z;
}

/* keyboard rotation, OpmanGUI Rotate (FUN_1803278f0, decomp/mastercam_keyrot.c), from Alt + arrow keys (FUN_1803222f0,
   WM_SYSKEYDOWN: Left (-1, 0), Right (1, 0), Up (0, 1), Down (0, -1)). a = h * rotate_fac deg turns the view about its up
   axis (rows 0 and 2), then b = v * rotate_fac deg about its right axis (rows 1 and 2). The screen centre is taken to
   world at the depth of the average displayed point (FUN_18013dbd0; else the construction depth) and back through the
   new matrix. */
void view_rotate_keys(int h, int v, double rot_deg) {
    if (!h && !v) return;
    double a = h * rot_deg * 0.01745329251994329, b = v * rot_deg * 0.01745329251994329;
    double c = cos(a), sn = sin(a), c3 = cos(b), s3 = sin(b), N[9]; memcpy(N, vmat, sizeof N);
    if (h) for (int k = 0; k < 3; k++) { N[k] = sn * vmat[6 + k] + c * vmat[k]; N[6 + k] = c * vmat[6 + k] - sn * vmat[k]; }
    if (v) { double r1[3], r2[3]; for (int k = 0; k < 3; k++) { r1[k] = N[3 + k]; r2[k] = N[6 + k]; }
        for (int k = 0; k < 3; k++) { N[3 + k] = s3 * r2[k] + c3 * r1[k]; N[6 + k] = c3 * r2[k] - s3 * r1[k]; } }
    double z = view_mean_depth();
    double P[3]; view_to_world3(vcx, vcy, z, P);
    memcpy(vmat, N, sizeof N);
    double pv[3]; world_to_view3(P, pv); vcx = pv[0]; vcy = pv[1];
}

/* Matrix33::GetViewNo: the reserved view whose matrix this is, else 0 */
int view_number(const double m[9]) {
    for (int n = 1; n <= 8; n++) { int k = 0; while (k < 9 && fabs(m[k] - STD_VIEW[n][k]) < 1e-6) k++; if (k == 9) return n; }
    return 0;
}

/* ---- animated change to a standard view: CGraphicsView FUN_1801ccd70 (decomp/mastercam_viewtrans.c), called by
   COpStatusBar::CommonPlaneMenu for Top / Front / Right / Iso (decomp/mastercam_viewcmd2.c) when config 33006 "Animate
   view transitions" = y. Matrices here are CncGaf::MtTmat3d (decomp/gafmt_tmat.c, gafmt_minangle.c): T = V^T. */
static struct { int on; double t0; double a[3], b[3]; } za;
int view_anim = 1;                                          /* config 33006 (GetAllowViewAnimation) */
static void m3mul(const double a[9], const double b[9], double o[9]) {
    double t[9]; for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) t[3 * r + c] = a[3 * r] * b[c] + a[3 * r + 1] * b[3 + c] + a[3 * r + 2] * b[6 + c];
    memcpy(o, t, sizeof t);
}
static void m3tr(const double a[9], double o[9]) { double t[9]; for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) t[3 * c + r] = a[3 * r + c]; memcpy(o, t, sizeof t); }
/* MtTmat3d::SetRotation(short axis, a) */
static void m3rot(int ax, double a, double o[9]) {
    double c = cos(a), s = sin(a); double I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; memcpy(o, I, sizeof I);
    if (ax == 0) { o[4] = c; o[5] = s; o[7] = -s; o[8] = c; }
    else if (ax == 1) { o[0] = c; o[2] = -s; o[6] = s; o[8] = c; }
    else { o[0] = c; o[1] = s; o[3] = -s; o[4] = c; }
}
static void v3n(double v[3]) { double l = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; } }
/* MtTmat3d::GetAngleDiff(order, other, ax, ay, az) (gafmt 0x11a7e0). Odd orders use the inverse product and negate;
   the product order was confirmed numerically: every order rebuilds T1 = R_i R_j R_k T0 (1200 random cases). */
static void angle_diff(int order, const double T0[9], const double T1[9], double ang[3]) {
    double D[9], It[9], R[9], v[3];
    if (order & 1) { m3tr(T0, It); m3mul(T1, It, D); } else { m3tr(T1, It); m3mul(T0, It, D); }
    if (order <= 1) {
        memcpy(v, D + 6, sizeof v); v3n(v); ang[0] = atan2(v[1], v[2]); ang[1] = -asin(v[0]);
        m3rot(0, ang[0], R); m3mul(D, R, D); m3rot(1, ang[1], R); m3mul(D, R, D);
        memcpy(v, D + 3, sizeof v); v3n(v); ang[2] = atan2(v[0], v[1]);
    } else if (order <= 3) {
        memcpy(v, D, sizeof v); v3n(v); ang[1] = atan2(v[2], v[0]); ang[2] = -asin(v[1]);
        m3rot(1, ang[1], R); m3mul(D, R, D); m3rot(2, ang[2], R); m3mul(D, R, D);
        memcpy(v, D + 6, sizeof v); v3n(v); ang[0] = atan2(v[1], v[2]);
    } else {
        memcpy(v, D + 3, sizeof v); v3n(v); ang[2] = atan2(v[0], v[1]); ang[0] = -asin(v[2]);
        m3rot(2, ang[2], R); m3mul(D, R, D); m3rot(0, ang[0], R); m3mul(D, R, D);
        memcpy(v, D, sizeof v); v3n(v); ang[1] = atan2(v[2], v[0]);
    }
    if (order & 1) for (int k = 0; k < 3; k++) ang[k] = -ang[k];
}
/* axis order table built at DAT_1809a03e0 (values 0 / DAT_180876dc8 = 1 / DAT_180876dcc = 2) */
static const int AXES_ORDER[6][3] = { { 0, 1, 2 }, { 2, 1, 0 }, { 1, 2, 0 }, { 0, 2, 1 }, { 2, 0, 1 }, { 1, 0, 2 } };
/* MtTmat3d::GetMinAngleDiff: the order with the smallest |ax| + |ay| + |az| (first one wins, start 540) */
static int min_angle_diff(const double T0[9], const double T1[9], double ang[3]) {
    double best = 540; int bo = 0;
    for (int o = 0; o < 6; o++) { double a[3]; angle_diff(o, T0, T1, a); double s = fabs(a[0]) + fabs(a[1]) + fabs(a[2]); if (s < best) { best = s; bo = o; memcpy(ang, a, sizeof a); } }
    return bo;
}
/* StoredViews: the last centre / height of each reserved view, written by FUN_1801dd950 whenever the view is set
   while it is a reserved view, not animating and not rotating; read back as the transition's target. Here they are
   written when a transition starts (the values then are the last ones FUN_1801dd950 set in that view), unless they
   are the unchanged end of the previous transition (FUN_1801dd950 skips storing while +0x45a9 animating is set). */
static struct { int ok; double cx, cy, h; } stored_view[9];
static struct { int view; double cx, cy, h; } vt_end = { -1, 0, 0, 0 };
static struct { int on, order; double t0, T0[9], M1[9], ang[3], P[3], pv1[2], tc[2], th, h0; } vt;
/* FUN_1801bdd00: midpoint of the database extents (-10 / 10 when empty, FUN_1801d9810) and the visible solids, widened
   by 100 (x, y) / 200 (z) mm or 5 / 10 inch, a span under 0.01 opened by 1 each side, then min x 1.2 or 0.8 and max x
   1.2 or 0.8 by sign. Not added here (no source data in this viewer): STL entities (FUN_180228490), part-info origins,
   stock box (FUN_1803213e0), FUN_18031fea0 limits. */
static void trans_box_mid(double mid[3]) {
    double lo[3] = { 1e38, 1e38, 1e38 }, hi[3] = { -1e38, -1e38, -1e38 };
    #define ACCW(X, Y, Z) do { double p_[3] = { X, Y, Z }; for (int k_ = 0; k_ < 3; k_++) { if (p_[k_] < lo[k_]) lo[k_] = p_[k_]; if (p_[k_] > hi[k_]) hi[k_] = p_[k_]; } } while (0)
    for (int i = 0; i < F.npolys; i++) if (F.polys[i].kind == K_GEOM)
        for (int j = 0; j < F.polys[i].count; j++) { V3 q = F.pts[F.polys[i].first + j]; ACCW(q.x, q.y, q.z); }
    for (int k = 0; k < 3; k++) { if (lo[k] >= 1e38) lo[k] = -10; if (hi[k] <= -1e38) hi[k] = 10; }
    for (int i = 0; i < solid_ntri * 3; i++) ACCW(solid_tri[i * 6], solid_tri[i * 6 + 1], solid_tri[i * 6 + 2]);
    #undef ACCW
    int met = mcam_is_metric(&F);
    for (int k = 0; k < 3; k++) {
        double m = k < 2 ? (met ? 100 : 5) : (met ? 200 : 10);
        hi[k] += m; lo[k] -= m;
        if (fabs(hi[k] - lo[k]) < 0.01) { hi[k] += 1; lo[k] -= 1; }
        lo[k] *= lo[k] <= 0 ? 1.2 : 0.8; hi[k] *= hi[k] > 0 ? 1.2 : 0.8;
        mid[k] = (lo[k] + hi[k]) * 0.5;
    }
}
static void vt_apply(double s) {                            /* one step of the FUN_1801ccd70 loop at eased fraction s */
    double R[9], Rk[9], Ms[9];
    const int *o = AXES_ORDER[vt.order];
    m3rot(o[0], s * vt.ang[o[0]], R);
    m3rot(o[1], s * vt.ang[o[1]], Rk); m3mul(R, Rk, R);
    m3rot(o[2], s * vt.ang[o[2]], Rk); m3mul(R, Rk, R);
    m3mul(R, vt.T0, Ms);
    m3tr(Ms, vmat);                                         /* view rows = columns of the MtTmat3d */
    double pv[3]; world_to_view3(vt.P, pv);
    vcx = pv[0] + s * (vt.tc[0] - vt.pv1[0]); vcy = pv[1] + s * (vt.tc[1] - vt.pv1[1]);
    vht = vt.h0 + s * (vt.th - vt.h0);
}
void view_std_animated(int n) {
    if (n < 1 || n > 8) return;
    if (!view_anim) { view_std(n); return; }
    int cur = view_number(vmat);
    if (!memcmp(vmat, STD_VIEW[n], sizeof vmat)) return;    /* CommonPlaneMenu: only when the plane changes */
    if (cur && !(vt_end.view == cur && vt_end.cx == vcx && vt_end.cy == vcy && vt_end.h == vht)) {
        stored_view[cur].ok = 1; stored_view[cur].cx = vcx; stored_view[cur].cy = vcy; stored_view[cur].h = vht; }
    za.on = 0;
    double V0[9]; memcpy(V0, vmat, sizeof V0);
    m3tr(V0, vt.T0); double T1[9]; m3tr(STD_VIEW[n], T1);
    vt.order = min_angle_diff(vt.T0, T1, vt.ang);
    if (stored_view[n].ok) { vt.tc[0] = stored_view[n].cx; vt.tc[1] = stored_view[n].cy; vt.th = stored_view[n].h; }
    else {                                                  /* SetGrView(target); fit() with fit_toolpath off; back */
        ViewState sv; view_get(&sv); view_set_matrix(STD_VIEW[n]); int ft = fit_toolpath; fit_toolpath = 0; view_fit(0); fit_toolpath = ft;
        vt.tc[0] = vcx; vt.tc[1] = vcy; vt.th = vht; view_put(&sv);
    }
    double mid[3], mv[3]; trans_box_mid(mid); world_to_view3(mid, mv);
    view_to_world3(vcx, vcy, mv[2], vt.P);                  /* the current centre at the depth of the box middle */
    double pv1[3]; for (int r = 0; r < 3; r++) pv1[r] = STD_VIEW[n][3 * r] * vt.P[0] + STD_VIEW[n][3 * r + 1] * vt.P[1] + STD_VIEW[n][3 * r + 2] * vt.P[2];
    vt.pv1[0] = pv1[0]; vt.pv1[1] = pv1[1]; vt.h0 = vht;
    memcpy(vt.M1, STD_VIEW[n], sizeof vt.M1);
    if (view_anim_ms <= 0 || anim_delay <= 0) { vt_apply(1); memcpy(vmat, vt.M1, sizeof vmat); vt.on = 0; }
    else { vt.on = 1; vt.t0 = -1; }
    if (!vt.on) { vt_end.view = n; vt_end.cx = vcx; vt_end.cy = vcy; vt_end.h = vht; }
}

/* window pixel (x right, y down; inside the w x h viewport) <-> view coordinates (y up) */
void view_px_to_view(float px, float py, double *x, double *y) {
    double ppu = view_h / vht;
    *x = vcx + (px - view_w * 0.5) / ppu; *y = vcy - (py - view_h * 0.5) / ppu;
}
void view_to_world3(double vx, double vy, double vz, double out[3]) {   /* V is orthonormal: world = V^T * view */
    for (int k = 0; k < 3; k++) out[k] = vmat[k] * vx + vmat[3 + k] * vy + vmat[6 + k] * vz;
}
void world_to_view3(const double p[3], double out[3]) {
    for (int r = 0; r < 3; r++) out[r] = vmat[3 * r] * p[0] + vmat[3 * r + 1] * p[1] + vmat[3 * r + 2] * p[2];
}

/* FUN_1801cc890: (centre, height) to a new value; with "Animate zoom fits" the change runs over anim_delay * 12 steps
   of at least 17 ms (FUN_1801c92b0), the fraction eased by sin^2(t pi / 2) (FUN_1801c0a90 mode 1) */
static void zoom_to(double cx, double cy, double h) {
    double n = anim_delay * 12;
    if (!fit_anim || n <= 0 || view_anim_ms <= 0) { vcx = cx; vcy = cy; vht = h; za.on = 0; return; }
    za.a[0] = vcx; za.a[1] = vcy; za.a[2] = vht; za.b[0] = cx; za.b[1] = cy; za.b[2] = h; za.on = 1; za.t0 = -1;
}
float view_anim_ms = 1;                                     /* 0 = no animation (headless shots) */
void view_anim_step(double now_ms) {
    if (vt.on) {                                            /* FUN_1801ccd70 loop: anim_delay * 12 steps of >= 17 ms, */
        if (vt.t0 < 0) vt.t0 = now_ms;                      /* fraction eased by FUN_1801c0a90 mode 0: 1 - cos(t pi/2) */
        double t = (now_ms - vt.t0) / (anim_delay * 12 * 17.0); if (t > 1) t = 1;
        double s = 1 - cos(t * M_PI / 2); if (s > 1) s = 1; if (s < 0) s = 0;
        vt_apply(s);
        if (t >= 1) { memcpy(vmat, vt.M1, sizeof vmat); vt.on = 0; vt_end.view = view_number(vmat); vt_end.cx = vcx; vt_end.cy = vcy; vt_end.h = vht; }
        return;
    }
    if (!za.on) return;
    if (za.t0 < 0) za.t0 = now_ms;
    double t = (now_ms - za.t0) / (anim_delay * 12 * 17.0); if (t > 1) t = 1;
    double s = sin(t * M_PI / 2); s *= s;
    vcx = za.a[0] + s * (za.b[0] - za.a[0]); vcy = za.a[1] + s * (za.b[1] - za.a[1]); vht = za.a[2] + s * (za.b[2] - za.a[2]);
    if (t >= 1) za.on = 0;
}

/* zoom history for Unzoom previous (CGraphicsView +0x38b0: centre x, centre y, height) */
static double zhist[64][3]; static int nzhist, zhist_wheel;
void view_zoom_push(void) {
    if (nzhist == 64) { memmove(zhist, zhist + 1, sizeof zhist[0] * 63); nzhist--; }
    zhist[nzhist][0] = vcx; zhist[nzhist][1] = vcy; zhist[nzhist][2] = vht; nzhist++;
}
void view_wheel_run_end(void) { zhist_wheel = 0; }

/* OnScreenUnzoomPrevious (FUN_180329a10): the last pushed zoom, else twice the height */
void view_unzoom_previous(void) {
    zhist_wheel = 0;
    if (nzhist > 0) { nzhist--; zoom_to(zhist[nzhist][0], zhist[nzhist][1], zhist[nzhist][2]); }
    else zoom_to(vcx, vcy, fmin(vht * 2, 1e17));
}
/* OnScreenUnzoom (FUN_1803297c0 with 0.8): height / 0.8 about the screen centre */
void view_unzoom(double f) { zoom_to(vcx, vcy, fmin(vht / f, 1e17)); }

/* ZoomUnzoom (FUN_18032a080): n notches of the zoom factor about a point (view coordinates); zooming in saves the view
   for Unzoom previous, once per run of wheel zooms (+0x38d8) */
void view_zoom_steps(int n, double px, double py, int wheel) {
    if (n > 0 && !(wheel && zhist_wheel)) view_zoom_push();
    if (n > 0) zhist_wheel = wheel != 0;
    double s = n * cfg_zoom_fac;
    vht *= 1.0 - s;
    vcx += s * (px - vcx); vcy += s * (py - vcy);
}

/* GetFitExtents (FUN_180320770): extents of the visible entities in view coordinates; width and height x 1.05, the
   height grown to the window's aspect; nothing / tiny -> 250 (metric) or 10 (inch) wide */
static void fit_acc(void *u, double x, double y, double z) {   /* one point into GetFitExtents' box (u = lo x, lo y, hi x, hi y) */
    double *lh = u, p[3] = { x, y, z }, v[3]; world_to_view3(p, v);
    if (v[0] < lh[0]) lh[0] = v[0]; if (v[0] > lh[2]) lh[2] = v[0]; if (v[1] < lh[1]) lh[1] = v[1]; if (v[1] > lh[3]) lh[3] = v[1];
}
void view_fit(int animate) {
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    #define ACC(X, Y, Z) do { double p_[3] = { X, Y, Z }, v_[3]; world_to_view3(p_, v_); \
        if (v_[0] < lo[0]) lo[0] = v_[0]; if (v_[0] > hi[0]) hi[0] = v_[0]; if (v_[1] < lo[1]) lo[1] = v_[1]; if (v_[1] > hi[1]) hi[1] = v_[1]; } while (0)
    for (int i = 0; i < F.npolys; i++) {
        const Poly *p = &F.polys[i];
        if (p->kind == K_GEOM && !GEOM_SHOWN(p)) continue;   /* FUN_180320770 walks the display list: visible levels only (mcam_load.h) */
        if (p->kind != K_GEOM) {                          /* fit_toolpath 1: + the displayed toolpaths' backplot */
            if (!fit_toolpath || !show_tp || (sel >= 0 && p->op != F.ops[sel].id) || (p->kind == K_RAPID && !show_rapid)) continue;
        }
        for (int j = 0; j < p->count; j++) { V3 q = F.pts[p->first + j]; ACC(q.x, q.y, q.z); }
    }
    for (int i = 0; i < solid_ntri * 3; i++) ACC(solid_tri[i * 6], solid_tri[i * 6 + 1], solid_tri[i * 6 + 2]);
    for (int i = 0; i < user_ntri * 3; i++) ACC(user_tri[i * 6], user_tri[i * 6 + 1], user_tri[i * 6 + 2]);
    for (int s = 0; s < surf_nmeshed; s++) if (surf_tn && surf_tn[s] && surf_shown(s))   /* the displayed surfaces (entities, as the wireframe) */
        for (int i = surf_tfirst[s] * 3; i < (surf_tfirst[s] + surf_tn[s]) * 3; i++) ACC(surf_tri[i * 6], surf_tri[i * 6 + 1], surf_tri[i * 6 + 2]);
    if (pm_dirty || pm_nm != F.nmeshes) pm_build();
    for (int m = 0; m < pm_nm; m++) if (pm_n && pm_n[m] && pm_shown(m))   /* the displayed meshes */
        for (int i = pm_first[m] * 3; i < (pm_first[m] + pm_n[m]) * 3; i++) ACC(pm_tri[i * 6], pm_tri[i * 6 + 1], pm_tri[i * 6 + 2]);
    if (scene_fit_extra) { double lh[4] = { lo[0], lo[1], hi[0], hi[1] }; scene_fit_extra(fit_acc, lh);   /* FUN_180337650: the stock (no copy) */
        lo[0] = lh[0]; lo[1] = lh[1]; hi[0] = lh[2]; hi[1] = lh[3]; }
    #undef ACC
    double asp = (double)view_w / (view_h > 0 ? view_h : 1), cx = vcx, cy = vcy, w, h;
    if (lo[0] <= hi[0] && hypot(hi[0] - lo[0], hi[1] - lo[1]) >= 5 * 0.0025) {   /* 5 x tol (formats/ui.md: tolerance) */
        cx = (lo[0] + hi[0]) * 0.5; cy = (lo[1] + hi[1]) * 0.5;
        w = (hi[0] - lo[0]) * 1.05; h = (hi[1] - lo[1]) * 1.05;
        if (w > h * asp) h = w / asp;
    } else { w = !mcam_is_metric(&F) ? 10 : 250; h = w / asp; }
    if (animate) zoom_to(cx, cy, h); else { vcx = cx; vcy = cy; vht = h; za.on = 0; }
}

/* free dynamic rotation (FUN_1801b0bd0 mode -1, CGraphicsView::RotateObject): a = dx 2pi / width, e = dy 2pi / height
   (pixels, y up), V' = R V with R = [ca, -se sa, ce sa; 0, ce, se; -sa, -se ca, ce ca]; the pivot keeps its screen place */
void view_rotate_free(double dx, double dy_up, const double pivot[3]) {
    double before[3], after[3]; world_to_view3(pivot, before);
    double a = dx * 2 * M_PI / view_w, e = dy_up * 2 * M_PI / view_h, ca = cos(a), sa = sin(a), ce = cos(e), se = sin(e);
    double R[9] = { ca, -se * sa, ce * sa, 0, ce, se, -sa, -se * ca, ce * ca }, M[9];
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) M[3 * r + c] = R[3 * r] * vmat[c] + R[3 * r + 1] * vmat[3 + c] + R[3 * r + 2] * vmat[6 + c];
    memcpy(vmat, M, sizeof M);
    world_to_view3(pivot, after);
    vcx += after[0] - before[0]; vcy += after[1] - before[1];
}
/* turntable rotation (mode 0, config 33002 'free' mode = n): azimuth / elevation accumulate, matrix rebuilt */
static double rot_az, rot_el;
void view_rotate_begin(void) {
    rot_az = atan2(vmat[1], vmat[0]); if (rot_az < 0) rot_az += 2 * M_PI;
    rot_el = atan2(vmat[5], vmat[8]); if (rot_el < 0) rot_el += 2 * M_PI;
}
void view_rotate_turntable(double dx, double dy_up, const double pivot[3]) {
    double before[3], after[3]; world_to_view3(pivot, before);
    rot_az += dx * 2 * M_PI / view_w; rot_el += dy_up * 2 * M_PI / view_h;
    double ca = cos(rot_az), sa = sin(rot_az), ce = cos(rot_el), se = sin(rot_el);
    double M[9] = { ca, sa, 0, -ce * sa, ce * ca, se, se * sa, -se * ca, ce };
    memcpy(vmat, M, sizeof M);
    world_to_view3(pivot, after);
    vcx += after[0] - before[0]; vcy += after[1] - before[1];
}
/* rotation about one axis while Ctrl / X / Y / Z is held during a free-mode middle drag (CGraphicsView::RotateObject,
   GetAsyncKeyState 0x11 / 'X' / 'Y' / 'Z' -> mode 1-4; FUN_1801cf1c0, decomp/mastercam_axisrot.c). Axis in view
   coordinates: Ctrl the screen normal, X / Y / Z the world axis through the view at the time the key went down
   (MtVector3d * (WCS^-1 * view); WCS = Top here), turned to point at the viewer. Once the cursor is 1 px from the
   pivot's screen point the start view T0, the pivot in view coordinates and the centre are kept, and the view becomes
   R(axis, angle - angle0) applied in view space (MtTmat3d::SetRotation(MtVector3d, a)), the angle being the cursor's
   direction about the pivot's screen point; the centre moves so the pivot stays put. A change of key restarts. */
static struct { int mode, started; double axis[3], V0[9], pv0[3], c0[2], a0, sx, sy; } axr;
void view_axis_rotate_begin(const double pivot[3]) {        /* button down: pivot to screen (uu_to_scrn), y up */
    double pv[3]; world_to_view3(pivot, pv); double ppu = view_h / vht;
    axr.sx = view_w * 0.5 + (pv[0] - vcx) * ppu; axr.sy = view_h * 0.5 + (pv[1] - vcy) * ppu; axr.started = 0; axr.mode = 0;
}
int view_axis_rotate(int mode, double mx, double my_up, const double pivot[3]) {
    if (mode != axr.mode && axr.started) { axr.mode = 0; axr.started = 0; }
    if (!mode) { axr.mode = 0; return 0; }
    if (mode != axr.mode) {
        axr.mode = mode;
        if (mode == 1) { axr.axis[0] = 0; axr.axis[1] = 0; axr.axis[2] = 1; }
        else { int k = mode - 2; for (int r = 0; r < 3; r++) axr.axis[r] = vmat[3 * r + k]; }
        if (axr.axis[2] < 0) for (int r = 0; r < 3; r++) axr.axis[r] = -axr.axis[r];
    }
    double dx = mx - axr.sx, dy = my_up - axr.sy;
    if (!axr.started) {
        if (dx * dx + dy * dy < 1.0) return 1;
        axr.started = 1; memcpy(axr.V0, vmat, sizeof vmat); world_to_view3(pivot, axr.pv0);
        axr.c0[0] = vcx; axr.c0[1] = vcy; axr.a0 = atan2(dy, dx);
    }
    double a = atan2(dy, dx) - axr.a0, ax[3] = { axr.axis[0], axr.axis[1], axr.axis[2] };
    double l = sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]); if (l > 0) for (int k = 0; k < 3; k++) ax[k] /= l;
    double c = cos(a), sn = sin(a), t = 1 - c, x = ax[0], y = ax[1], z = ax[2];
    double R[9] = { t * x * x + c, t * x * y - z * sn, t * x * z + y * sn,          /* the gaf matrix is this transposed */
                    t * x * y + z * sn, t * y * y + c, t * y * z - x * sn,
                    t * x * z - y * sn, t * y * z + x * sn, t * z * z + c };
    for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) vmat[3 * r + k] = R[3 * r] * axr.V0[k] + R[3 * r + 1] * axr.V0[3 + k] + R[3 * r + 2] * axr.V0[6 + k];
    double u[3]; for (int r = 0; r < 3; r++) u[r] = R[3 * r] * axr.pv0[0] + R[3 * r + 1] * axr.pv0[1] + R[3 * r + 2] * axr.pv0[2];
    vcx = u[0] + axr.c0[0] - axr.pv0[0]; vcy = u[1] + axr.c0[1] - axr.pv0[1];
    return 1;
}
/* pan with the cursor (FUN_1801b0bd0 mode 1): centre += (previous - current) / pixels per unit */
void view_pan_px(double dx, double dy_up) { double ppu = view_h / vht; vcx -= dx / ppu; vcy -= dy_up / ppu; }
/* Pan (FUN_180325ea0): steps x pan factor (config 4028) x height */
void view_pan_steps(int sx, int sy, double pan_fac) { vcx += fabs(vht) * sx * pan_fac; vcy += fabs(vht) * sy * pan_fac; }

/* model units spanned by the view's height */
float scene_view_height(void) { return (float)vht; }

static int m4_inv(const float *m, float *inv) {
    double t[16], det;
    t[0] = m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
    t[4] = -m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
    t[8] = m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
    t[12] = -m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
    t[1] = -m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
    t[5] = m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
    t[9] = -m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
    t[13] = m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
    t[2] = m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
    t[6] = -m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
    t[10] = m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
    t[14] = -m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
    t[3] = -m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
    t[7] = m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
    t[11] = -m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
    t[15] = m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
    det = m[0]*t[0]+m[1]*t[4]+m[2]*t[8]+m[3]*t[12];
    if (fabs(det) < 1e-30) return 0;
    for (int i = 0; i < 16; i++) inv[i] = (float)(t[i] / det);
    return 1;
}

/* screen point (window pixels, y down) inside viewport x,y,w,h (GL coords, y up) -> world point on plane Z = 0 */
int scene_pick(int x, int y, int w, int h, float mx, float my, double out[3]) {
    M4 mvp = camera(w, h); float inv[16];
    if (!m4_inv(mvp.m, inv)) return 0;
    (void)y;
    float nx = 2 * (mx - x) / w - 1, ny = 1 - 2 * my / h;
    double p[2][3];
    for (int k = 0; k < 2; k++) {
        float z = k ? 1.0f : -1.0f, v[4] = { nx, ny, z, 1 }, r[4];
        for (int i = 0; i < 4; i++) r[i] = inv[i] * v[0] + inv[4 + i] * v[1] + inv[8 + i] * v[2] + inv[12 + i] * v[3];
        for (int i = 0; i < 3; i++) p[k][i] = r[i] / r[3];
    }
    double z0 = grid_z() - ctrz, dz = p[1][2] - p[0][2];      /* the grid plane: what you click on is what you get */
    double t = fabs(dz) > 1e-12 ? (z0 - p[0][2]) / dz : 0;
    out[0] = p[0][0] + t * (p[1][0] - p[0][0]) + ctrx;
    out[1] = p[0][1] + t * (p[1][1] - p[0][1]) + ctry;
    out[2] = grid_z();
    return 1;
}

void scene_remesh_solids(void) { mesh_solids(); }   /* the part got solids after new_file (cad_import.c) */
void scene_reupload(void) { keep_view = 1; upload(); keep_view = 0; if (loaded && F.nnotes) scene_notes_rebuild(); }   /* notes (E_NOTE): the
                                                                         drafting layer follows the entities (deleted / restored / attributes) */

/* new rotation centre: the vertices are stored relative to the centre, so re-upload around it, keeping the view */
void scene_set_center(double x, double y, double z) {
    ctrx = (float)x; ctry = (float)y; ctrz = (float)z;
    lock_ctr = 1; keep_view = 1; upload(); keep_view = 0;
}

void scene_pixel_to_world(int w, int h, float mx, float my, double out[3]) { scene_pick(0, 0, w, h, mx, my, out); }

/* Transform > Dynamic's gnomon (core_ui cui_xdyn.c gn_upload): lit, alpha-blended triangles + lines, world coords; drawn last, over
   everything, with a depth buffer of its own */
void scene_set_gnomon(const float *tri, int nt, const float *lin, int nl) {
    free(gnt_v); gnt_v = NULL; gnt_n = 0; free(gnl_v); gnl_v = NULL; gnl_n = 0;
    if (nt > 0 && (gnt_v = malloc((size_t)nt * 40))) { memcpy(gnt_v, tri, (size_t)nt * 40); gnt_n = nt; }
    if (nl > 0 && (gnl_v = malloc((size_t)nl * 24))) { memcpy(gnl_v, lin, (size_t)nl * 24); gnl_n = nl; }
}
void scene_set_overlay(const float *v, int n) {
    free(ovl); ovl = NULL; novl = 0;
    if (n > 0 && (ovl = malloc((size_t)n * 24))) { memcpy(ovl, v, (size_t)n * 24); novl = n; }   /* no memory: no overlay (ours) */
}

/* the verify simulation's stock mesh (verify.c vf_mesh: 9 floats per vertex, world coords); n = 0 hides it. Z0/Z1 = the stock's
   world z range for the depth tint (ours). */
void scene_set_verify(const float *v, int n, const float rgb[3], double z0, double z1) {
    vf_n = 0;
    if (rgb) memcpy(vf_rgb, rgb, 12);
    vf_z[0] = z0; vf_z[1] = z1;
    if (n <= 0) return;
    float *cv = malloc((size_t)n * 36);
    for (int i = 0; i < n; i++) { memcpy(cv + i * 9, v + i * 9, 36); cv[i * 9] -= ctrx; cv[i * 9 + 1] -= ctry; cv[i * 9 + 2] -= ctrz; }
    glBindBuffer(GL_ARRAY_BUFFER, vfbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * 36, cv, GL_STATIC_DRAW);
    free(cv);
    vf_n = n;
}

void scene_set_dash(const float *v, int n) {
    float *c = n > 0 && v ? malloc((size_t)n * 24) : NULL;
    if (c) memcpy(c, v, (size_t)n * 24);
    scene_take_dash(c, c ? n : 0);                    /* no memory: no dashed geometry (ours) */
}
void scene_take_dash(float *v, int n) {              /* the buffer becomes the scene's (freed here; ours: no copy) */
    free(dashv); dashv = NULL; ndash = 0;
    if (n > 0 && v) {
        for (int i = 0; i < n; i++) { v[i * 6] -= ctrx; v[i * 6 + 1] -= ctry; v[i * 6 + 2] -= ctrz; }
        dashv = v; ndash = n; dash_ctr[0] = ctrx; dash_ctr[1] = ctry; dash_ctr[2] = ctrz;
    } else free(v);
}

/* world point -> window pixel inside a viewport of size w x h (origin top-left of the viewport) */
/* the projection of the last scene_to_screen call, kept while its inputs are the same (ours, speed: the AutoCursor projects
   thousands of points per mouse move; the matrix is rebuilt bit-identically when anything changes) */
static struct { int w, h; double v[9], cx, cy, cz, vcx, vcy, vht, ext; M4 m; int ok; } cam_c;
static unsigned cam_serial;
static const M4 *camera_cached(int w, int h) {
    if (!cam_c.ok || cam_c.w != w || cam_c.h != h || memcmp(cam_c.v, vmat, sizeof cam_c.v) || cam_c.cx != ctrx || cam_c.cy != ctry || cam_c.cz != ctrz
        || cam_c.vcx != vcx || cam_c.vcy != vcy || cam_c.vht != vht || cam_c.ext != ext) {
        cam_c.m = camera(w, h); cam_c.w = w; cam_c.h = h; memcpy(cam_c.v, vmat, sizeof cam_c.v); cam_c.cx = ctrx; cam_c.cy = ctry; cam_c.cz = ctrz;
        cam_c.vcx = vcx; cam_c.vcy = vcy; cam_c.vht = vht; cam_c.ext = ext; cam_c.ok = 1; cam_serial++;
    }
    return &cam_c.m;
}
unsigned scene_view_serial(int w, int h) { (void)camera_cached(w, h); return cam_serial; }
int scene_to_screen(int w, int h, double x, double y, double z, float *sx, float *sy) {
    M4 mvp = *camera_cached(w, h);
    float p[4] = { (float)(x - ctrx), (float)(y - ctry), (float)(z - ctrz), 1 }, r[4];
    for (int i = 0; i < 4; i++) r[i] = mvp.m[i] * p[0] + mvp.m[4 + i] * p[1] + mvp.m[8 + i] * p[2] + mvp.m[12 + i] * p[3];
    if (r[3] <= 1e-9f) return 0;
    *sx = (r[0] / r[3] * 0.5f + 0.5f) * w;
    *sy = (0.5f - r[1] / r[3] * 0.5f) * h;
    return 1;
}
