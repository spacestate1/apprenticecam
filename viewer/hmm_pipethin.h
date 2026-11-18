/* hmm_pipethin - port of mchmm.dll's pass writer CPipeThinToolpath (the object at Eva +0x4f0 that FUN_180557f60 writes the Voronoi
 * offset loops through, and at Eva +0x1440 for the angle filter's loops) and its flush into TGridC pass records FUN_18011ca00
 * (formats/hst3d_port_plan.md 7 "W6"). Verified against the DLL by tools/tests/hmm_voronoi_oracle.sh (modes pipe0 / pipe1 / pipe2:
 * peloader --hmmvor with the real writer, the records dumped by hvd_pipe). */
#ifndef HMM_PIPETHIN_H
#define HMM_PIPETHIN_H
#include <stdint.h>
#include "hmm_voronoi.h"

/* the curve template (Eva +0x4a8, FUN_1802f2340 dis 1802f2402-1802f278f), copied into the writer at +0x28 */
typedef struct {
    int mode;        /* ThinCurveTolerance 0x1ad0 != 0 ? (ThinArcEnable 0x1ad4 ? 2 : 1) : 0 (FUN_1802eaec0) */
    double ctol;     /* +8 ThinCurveTolerance */
    double cang;     /* +0x10 cos(ThinAngleTolerance 0x1ad3) */
    double smin;     /* +0x18 ThinSegmentLengthMin 0x1ad2 */
    double smax;     /* +0x20 ThinSegmentLengthMax 0x1ad1 */
    double amax;     /* +0x28 ThinArcMaxRadius 0x1ec7 */
    int senab;       /* +0x30 StraightLineEnable 0x1c7f */
    double scos;     /* +0x38 cos(StraightLineAngleTolerance 0x1c7e) */
    double ssin;     /* +0x40 sin(StraightLineAngleTolerance) */
} HptTemplate;

typedef struct HptWriter HptWriter;
/* the writer as FUN_180406b00 sets it up (hz l.8943-8985): z = the level (+0xd0), pco = PosContactOnly 0x1ae1 (+0x114) */
HptWriter *hpt_new(const HptTemplate *t, double z, int pco);
void hpt_free(HptWriter *w);
/* the angle filter's writer (Eva +0x1440, FUN_18040b6f0): z 0, PosContactOnly byte 1, the flag rule from Eva +0x15c0; flushed by
   FUN_18011cbc0 (hpt_flush: the same thinning and records, its +0x178 is never set by lines) */
HptWriter *hpt_new_filter(const HptTemplate *t, int e15c0);
/* the HvWriter callbacks (FUN_1801192a0 / FUN_1801194d0 / FUN_1801195f0 / FUN_180121900) writing into w */
void hpt_hvwriter(HptWriter *w, HvWriter *out);
/* the writer's near-full-arc flag +0x178 (FUN_1801195f0: an arc of pseudo-angle >= 3.9 without its byte 6) */
int hpt_f178(const HptWriter *w);

/* FUN_18011ca00's output: the points (2D, passes +0xa8 with +8 = 1) and the TGridC records (passes +0xe8) */
typedef struct {
    int kind;        /* the start call's kind (0x1e / 0x1f) */
    long first, n;   /* first point index, vertex count (a closed record keeps n but its last point is popped) */
    int closed;      /* +0x20 */
    long vb;         /* index of its first vertex's bits in HptPasses.bits */
} HptRec;
typedef struct {
    double *xy; long npt, cpt;
    HptRec *rec; int nrec, crec;
    uint64_t *bits; long nbits, cbits;   /* per record vertex: 0, 0x40000000 (span flag -1), 0x20000000 (mixed), arc centres
                                            0x4000000 / 0x5000000 (FUN_180123330) */
} HptPasses;
/* FUN_18011ca00: thinning (FUN_18011d020, template mode != 0), the records (FUN_18011ae20 / FUN_180123330), then the writer is
   cleared (FUN_180118930). With +0x178 set the curves are dropped first (message 0x4c4) and no record is written. Returns 1, or 0
   where the step is NOT DECODED (msg says which). */
int hpt_flush(HptWriter *w, HptPasses *out, char *msg, int nmsg);
void hpt_passes_free(HptPasses *p);
/* FUN_18011cbc0(writer, .., 1): the thinning and the records as hpt_flush, the curves kept (FUN_180407c10's first and second flush of the angle
   filter's writer); hpt_clear = FUN_180118930 */
int hpt_records(HptWriter *w, HptPasses *out, char *msg, int nmsg);
void hpt_clear(HptWriter *w);
/* FUN_18011ce60(writer, the ToolBoxSet of the other polygons) (FUN_180407c10's fallback, hzp l.411): the crossings of each curve's composite
   chords removed by splitting (FUN_180123be0), then curve 0's chords against the elements segs (4 doubles each: x0 y0 x1 y1; FUN_180123ea0).
   Returns the count (the DLL's result is count > 0), -1 where the port refuses (a split leaving an empty part, a curve that is not lines) */
int hpt_fix_isect(HptWriter *w, const double *segs, int nseg);
/* coverage counters of the mode-2 (arc) thinning paths, printed to stderr (oracle statistics) */
void hpt_stats(void);

#endif
