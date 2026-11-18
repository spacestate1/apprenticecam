/* toollib - tool libraries: Mastercam 2017 .tooldb (SQLite, read-only) and our own text format (.tools, read/write),
 * holder outlines, tool outlines for display, feeds/speeds. See ../formats/tooldb.md. */
#ifndef TOOLLIB_H
#define TOOLLIB_H
#include <stdio.h>
#include "mcam_load.h"

typedef struct { char name[48]; double *rz; int n; } Holder;    /* outline (radius, height) from the gauge line up */

typedef struct {
    char path[1024];
    int metric;                 /* 1 mm, 0 inch, -1 unknown */
    Tool *t; int n;
    Holder *h; int nh;
    unsigned char (*del)[16]; int ndel;     /* .tooldb rows of tools removed since loading (deleted on save) */
} ToolLib;

int toollib_load(const char *path, ToolLib *L, char *err, int errn);   /* .tooldb or .tools; 1 = ok */
int toollib_save(const char *path, const ToolLib *L);                  /* writes the .tools text format */
/* writes a Mastercam .tooldb (formats/toollib_net.md section 3): existing tools updated in place, new ones added as
   copies of DatabaseDefaults.tooldb's tool of that type (as the Tool Manager's AddTool), removed ones deleted; a new
   file starts as a copy of DatabaseDefaults.tooldb without its tools. 1 = ok. */
int toollib_save_tooldb(const char *path, ToolLib *L, char *err, int errn);
void toollib_free(ToolLib *L);
/* the Tool Manager's Compress (TlToolLibrarySQL vtable slot 28, FUN_1807e8f80): COMMIT, then "VACUUM;" on the .tooldb. 1 = ok */
int toollib_vacuum(const char *path, char *err, int errn);
int toollib_add(ToolLib *L, const Tool *t);                            /* appends a copy, returns index */
void toollib_remove(ToolLib *L, int i);
/* standard places to look for libraries: $MCAM_TOOLLIB_DIR, ./tools, ~/.config/mcamui, the Windows disk's
 * Mastercam shared folders. Returns the count of found paths (caller frees each and the array). */
int toollib_find(char ***paths);

void tool_write(FILE *o, const Tool *t);                 /* "TOOL k=v ... name=..." (+ "THOLDER ...") lines */
int tool_parse(const char *line, Tool *t);               /* 1 = a valid TOOL line */
int tool_read_holder(const char *line, Tool *t);         /* a THOLDER line following it */
const char *tool_type_name(int type);
int tool_type_count(void);
int tool_is_drill(int type);              /* mccore Cnc::Tool::is_drill_tool: every type that is not a mill tool (0-9, 20, 22) */
int tool_is_mill(int type);               /* mccore Cnc::Tool::is_mill_tool: 10-19, 21, 23, 24, 25 */
void tool_copy(Tool *dst, const Tool *src);   /* deep copy (holder outline) */
void tool_set_holder(Tool *t, const Holder *h);
/* side outline of the cutter + holder as (radius, z) pairs from the tip up, z = 0 at the tip; returns count */
int tool_outline(const Tool *t, double *r, double *z, int max);
/* a new tool: Mastercam's default for the type and units (DatabaseDefaults.tooldb); dia > 0 replaces the diameter.
   1 = found */
int tool_new(Tool *t, int type, double dia, int num, int metric);
int toollib_catalog_materials(char (*names)[32], int max);   /* DatabaseCatalog.tooldb tool materials by name */
typedef struct { char name[64]; double dia, flute, oal, shank, drill_dia, drill_len, thread_info; } StdSize;
int toollib_catalog_sizes(int type, int metric, StdSize **out);   /* wizard standard sizes; caller frees *out */

/* Mastercam's legacy tool records built from a tool (decomp/mccore_tooladd.c): the 0x288-byte op_tool_info
   (TlTool / TlToolMill / subclass GetOpToolInfo, starting from t->op_info) and the 0x698-byte tp_tool = a TL_ID body
   (SetToOldTool). The caller sets t->slot first; op_tool_info +0x274 (machine group) is left to the caller. */
void tool_op_info(const Tool *t, unsigned char *b);
void tool_tp_tool(const Tool *t, unsigned char *tp);

#endif
