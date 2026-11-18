/* mp_parse_tables.h - predefined symbol tables (generated mp_parse_tables.c, tools/mp_gen_tables.py). ours */
#ifndef MP_PARSE_TABLES_H
#define MP_PARSE_TABLES_H
#include "mp_int.h"
typedef struct { const char *name, *label; unsigned f68, f78; double def; int nofs; } MpPredefVar;   /* nofs: AddVariable got no fs */
typedef struct { const char *name, *def; } MpPredefStr;
extern const MpPredefVar mp_predef_vars[]; extern const int mp_npredef_vars;
extern const MpPredefStr mp_predef_strs[]; extern const int mp_npredef_strs;
extern const char *const mp_predef_blocks[]; extern const int mp_npredef_blocks;
extern const MpOpDef mp_opdefs[]; extern const int mp_nopdefs;
#endif
