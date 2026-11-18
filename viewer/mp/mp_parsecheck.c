/* mp_parsecheck - ours: load .pst files with mp_post_load and print parse warnings (parser regression check).
   usage: mp_parsecheck [-v] FILE.pst... */
#include <stdio.h>
#include <string.h>
#include "mp_int.h"
static void dexpr(const MpPost *p, MpExpr *e)
{
    if (!e) return;
    int n; const MpOpDef *t = mp_op_table(&n);
    switch (e->kind) {
    case EX_NUM: printf("%g", e->num); break;
    case EX_STRLIT: printf("\"%s\"", e->str); break;
    case EX_VAR: printf("%s%s", e->prv ? "prv_" : "", p->vars[e->idx].name); break;
    case EX_STRVAR: printf("%s", p->strs[e->idx].name); break;
    case EX_UNOP: printf("(-"); dexpr(p, e->a); printf(")"); break;
    case EX_BINOP: printf("("); dexpr(p, e->a); printf(" %s ", t[e->op].name); dexpr(p, e->b); printf(")"); break;
    case EX_ASSIGN: dexpr(p, e->a); printf(" %s ", t[e->op].name); dexpr(p, e->b); break;
    case EX_FUNC: printf("%s(", t[e->op].name); for (int i = 0; i < e->nargs; i++) { if (i) printf(", "); dexpr(p, e->args[i]); } printf(")"); break;
    }
}
static void ditems(const MpPost *p, MpItem *it, int d)
{
    for (; it; it = it->next) {
        printf("%*s", d * 2, "");
        switch (it->code) {
        case EL_IF: case EL_WHILE: printf("%s ", it->code == EL_IF ? "IF" : "WHILE"); dexpr(p, it->expr); printf("\n");
            ditems(p, it->body, d + 1); if (it->has_else) { printf("%*sELSE\n", d * 2, ""); ditems(p, it->els, d + 1); } break;
        case EL_GROUP: printf("[\n"); ditems(p, it->body, d + 1); printf("%*s]\n", d * 2, ""); break;
        case EL_FORMULA: printf("= "); dexpr(p, it->expr); printf("\n"); break;
        case EL_CALL: case EL_CALLARGS: printf("CALL %s\n", p->blocks[it->idx].name); break;
        case EL_SQUOTE: case EL_DQUOTE: case EL_ASCII: printf("LIT %x '%s'\n", it->code, it->text); break;
        default:
            if (it->code >= 0x38c) printf("STR %x %s\n", it->code, p->strs[it->idx].name);
            else printf("VAR %x %s\n", it->code, p->vars[it->idx].name);
        }
    }
}
int main(int argc, char **argv)
{
    int verbose = 0, bad = 0;
    if (argc == 4 && !strcmp(argv[1], "--dump")) {
        char err[256]; MpPost *p = mp_post_load(argv[2], err, sizeof err);
        if (!p) return 1;
        int b = mp_find_block_p(p, argv[3]);
        if (b < 0) { printf("no block\n"); return 1; }
        ditems(p, p->blocks[b].body, 0);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) { verbose = 1; continue; }
        char err[512] = "";
        MpPost *p = mp_post_load(argv[i], err, sizeof err);
        if (!p) { printf("FAIL %s: %s\n", argv[i], err); bad++; continue; }
        int nb = 0, nd = 0; for (int b = 0; b < p->nblocks; b++) { nb++; nd += p->blocks[b].defined; }
        printf("%-4s %s: %d warnings, vars %d (+%d user), strings %d, blocks defined %d, fmts %d, fstrsel %d\n",
               p->nwarn ? "WARN" : "ok", argv[i], p->nwarn, p->nvars, p->nvars - p->npredef_vars, p->nstrs, nd, p->nfmts, p->nfstrsels);
        for (int w = 0; w < p->nwarn && (verbose || w < 8); w++) printf("    %s\n", p->warn[w]);
        if (p->nwarn) bad++;
    }
    return bad ? 1 : 0;
}
/* ours: --dump BLOCK prints the parsed post table of one postblock (appended debugging aid) */
