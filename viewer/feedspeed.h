/* feedspeed - Mastercam's feed / speed calculation (mccore calc_op_fs, class CFeedCalc) and its material libraries
 * (*.mcam-materials). Every rule and offset: ../formats/feeds_speeds.md. */
#ifndef FEEDSPEED_H
#define FEEDSPEED_H
#include <stddef.h>
#include "mcam_load.h"

typedef struct {                /* struct material (0x3a0 bytes, record body) */
    char name[0xc8];
    int units;                  /* +0xc8: 0 inch (ft/min, in/tooth), 1 / 2 metric (m/min, mm/tooth) */
    double sfm;                 /* +0xd0 base cutting speed */
    double sfm_pct[4];          /* +0xd8 per operation class (GetSFMIndex) */
    double fpt;                 /* +0xf8 base feed per tooth */
    double fpt_pct[26];         /* +0x100 per tool type */
    unsigned char tm_on[6];     /* +0x1d0 cutter materials 1..6 */
    double tm_sfm[6], tm_fpt[6];/* +0x1d8 / +0x208 */
} Material;

typedef struct { char path[1024]; Material *m; int n; } MatLib;

int matlib_load(const char *path, MatLib *L, char *err, size_t errn);   /* 1 = ok */
void matlib_free(MatLib *L);
const Material *matlib_find(const MatLib *L, const char *name);        /* exact name, NULL if absent */
/* the part's machine material library path ("\Materials\Default.materials-9") -> a file this OS opens: old
   extensions -> .mcam-materials (GetCurVerExtFromPrvVerExt), a leading separator = the shared data folder */
char *matlib_resolve(const char *machine_path, char *out, size_t n);

enum { FS_TOOL = 0, FS_MATERIAL = 1, FS_DEFAULTS = 2, FS_USER = 3 };   /* machine group Feed Calculation */
typedef struct {
    int mode;
    double user_rpm, user_feed, user_plunge, user_retract;              /* FS_USER */
} FsGroup;
typedef struct { int ok; int rpm; double feed, plunge, retract; } FsResult;

int fs_sfm_index(int opcode);                  /* GetSFMIndex: 0 drill, 1 contour, 2 pocket, 3 surface, -1 none */
int fs_op_skipped(int opcode);                 /* calc_op_fs leaves it alone (lathe / old NCI / no tool) */
/* calc_op_fs for one operation. op_rpm / op_feed: the operation's current values (mode From tool keeps them),
   t = its tool (plunge / retract in that mode, geometry otherwise), m = the group's material (FS_MATERIAL), feed_pct /
   speed_pct = the op's material adjustments (0 = none), ccw = counter-clockwise spindle, max_rpm <= 0 = no clamp.
   ok = 0: do not apply (nothing to change, or a value that would be 0 / not finite); ok = 1: all four valid. */
FsResult fs_calc(int opcode, int op_rpm, double op_feed, const Tool *t, const FsGroup *g, const Material *m,
                 double feed_pct, double speed_pct, int ccw, long max_rpm);

#endif
