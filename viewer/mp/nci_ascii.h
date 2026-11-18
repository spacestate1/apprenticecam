/* nci_ascii - Mastercam 2017's binary NCI -> ASCII .nci text conversion (mccore.dll bnci_to_nci @180088920,
 * bnci_convert_section = FUN_180083560 @180083560, bnci_to_string @180089b70 and its mccore line formatters).
 * Every rule, field and offset is cited in nci_ascii.c; the format is described in ../../formats/mp_engine.md,
 * section "ASCII NCI (bnci_to_nci port)". Records with the nci_bin +0xc flag 2 (Wire / Agie) and the lathe-only
 * (flag 1) formatters are NOT DECODED here (listed there). */
#ifndef MP_NCI_ASCII_H
#define MP_NCI_ASCII_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../mcam_load.h"

/* Values bnci_to_nci takes from live Mastercam state that a part file does not hold. mp_nci_env_default() fills the
   static initial values of the DLL globals (cited in nci_ascii.c); a caller can override them. */
typedef struct MpNciEnv {
    int n_places_nci;          /* mastercam.dll ?n_places_nci (static 8 @18087b884): fmt_real's "%.*lf" precision */
    double tol;                /* mccore ?tol (static 5e-05 @1812b5b58): modal feed comparison */
    int vers_no, m_vers_no;    /* 1050 line: mccore bytes @18143d54a / @18143d54b (static 22, 1) */
    char geo_filename[1024];   /* 1050 line: mccore ?geo_filename (the part path; written upper-cased). Default: McamFile.path */
    int have_date;             /* 1050 line: last-write time of geo_filename (local time); 0 = "0 0 0 0 0 0" */
    int day, mon, year, hour, min, sec;
    /* NOT DECODED runtime values (placeholders the caller can supply): */
    int crlf;                  /* Mastercam opens the .nci with _fsopen(name, "wt" / "at", 0x40) (FUN_1800a1800, SysFile::DoOpen
                                  @180a3ad70): text mode, i.e. CR LF line ends on Windows. 1 = write "\r\n" (ours: default 0, "\n") */
    /* overrides for values read from the part's machine definition (used when the part does not resolve them): */
    int max_spindle_rpm, have_max_rpm;   /* 1020: CCNCMachine::MaxToolSpindleRPM(op) */
    int axis_comb_id, have_axis_comb;    /* 950 before 1025: CAxisCombination::Id of AxisCombinationFromID(op +0x11bc) */
    int same_tool_answer;      /* CBnciPost::AssignToolChanges' yes/no prompt (same tool number, other tool): 1002 (yes,
                                  also the batch-mode answer) or 1000; 0 = 1002 */
    int home_set; double home[3];        /* 1003: overrides FUN_1800a0d70's home position (used when the op's view is
                                            not a reserved view 1..8, NOT DECODED) */
} MpNciEnv;

typedef struct MpNciStats {
    long records, lines;       /* binary records read / line pairs written */
    long unknown;              /* records convert_section reports as unknown gcodes (the op's conversion stops there) */
    long not_decoded;          /* records / values left out or filled with a placeholder (see warnings) */
    long ops, files;           /* ops converted, NC file groups (1050 headers) */
    long skipped;              /* ours: selected ops that wrote nothing or stopped early (made here = no OP_ID record, no NCI
                                  positions / section, probe op, an unknown gcode): the app refuses such a program */
    int skipped_op;            /* the 0-based F.ops index of the first of them (-1 none) */
} MpNciStats;

typedef struct MpNciConv MpNciConv;    /* modal state of one conversion (feed, flags); see nci_ascii.c */

void mp_nci_env_default(MpNciEnv *e, const McamFile *f);

/* One binary record -> its ASCII line pair as bnci_to_string writes it (param_4 = 1: gcode line + parameter line).
   CV carries the modal feed (param_5 = 1); NULL = a fresh state with param_5 = 0 (no modal feed).
   OPCODE = the op's TP_OPCODE (the -2 record's +0x20). Returns the number of line pairs (0 when bnci_to_string prints
   nothing; 1008 comments can give several, only the first is returned), -1 for a NOT DECODED record. */
int mp_nci_record_ascii(MpNciConv *cv, const uint8_t *nci_bin, size_t n, int opcode,
                        char *line1, size_t n1, char *line2, size_t n2);

MpNciConv *mp_nci_conv_new(const McamFile *f, const MpNciEnv *env);
void mp_nci_conv_free(MpNciConv *cv);

/* The whole .nci text bnci_to_nci writes for the operations F.ops[op_index[0..nops-1]] (0-based indexes; they are
   written in Operations Manager order = F.ops order). env NULL = mp_nci_env_default. stats / warn may be NULL;
   warn receives one line per NOT DECODED item. Returns 1 ok, 0 error (err). */
int mp_nci_write_part_env(const McamFile *f, const MpNciEnv *env, const int *op_index, int nops, FILE *out,
                          MpNciStats *stats, FILE *warn, char *err, size_t errn);
int mp_nci_write_part(const McamFile *f, const int *op_index, int nops, FILE *out, char *err, size_t errn);

#endif
