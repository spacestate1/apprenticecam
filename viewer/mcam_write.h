/* mcam_write - write Mastercam X9 / 2017 part files: a copy of an existing part with records added.
 * The source part is never modified. See ../formats/mcam.md and ../formats/mcam_write.md. */
#ifndef MCAM_WRITE_H
#define MCAM_WRITE_H
#include <stddef.h>
#include <stdint.h>

enum { SEG_ENT = 1, SEG_OOB = 2, SEG_SOLID_TAIL = 3, SEG_NCI = 4 };

typedef struct {
    int kind;                 /* SEG_ENT: u32 len + ent; SEG_OOB: data that follows a system ent (groups, materials);
                                 SEG_SOLID_TAIL: XT bytes + solid trailer; SEG_NCI: one binary NCI record (u16 + body) */
    size_t off, len;          /* in the source file */
    uint32_t cls; int32_t sub;    /* SEG_ENT: class, ASSOC / system subtype */
    int32_t id;               /* ent_idn (+0x34) of an entity record (ASSOC records too: their op id is at +0x68) */
} McwSeg;

typedef struct {
    uint8_t *d; size_t n;     /* the source file */
    McwSeg *seg; int nseg;
    int first_assoc, first_solid, first_nci;   /* segment indices (-1 if none) */
    int32_t max_ent_idn, max_op_id;
    int major, minor;         /* file version (0x12 X9 / .mcx-9, 0x13 2017 / .mcam) */
    char path[1024];
} McwFile;

int mcw_open(McwFile *m, const char *path, char *err, size_t errn);
int mcw_open_mem(McwFile *m, uint8_t *data, size_t n, char *err, size_t errn);   /* takes ownership of data (malloc) */
void mcw_close(McwFile *m);

/* an output: the source segments in order, with extra byte blocks inserted before given segment indices */
typedef struct { int before; uint8_t *bytes; size_t len; } McwInsert;
uint8_t *mcw_build(const McwFile *m, const char *keep, const McwInsert *ins, int nins, size_t *outn);
int mcw_write(const McwFile *m, const McwInsert *ins, int nins, const char *path, char *err, size_t errn);

#endif
