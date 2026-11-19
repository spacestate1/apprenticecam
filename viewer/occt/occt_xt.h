/* occt_xt - OpenCASCADE B-rep -> Parasolid binary transmit (XT) exactly as Mastercam 2017 embeds it in a SOLIDXMT
 * record (formats/mcam.md "Solids", formats/xt.md, formats/occt_xt.md).  The inverse of xt_occt.h.
 *
 * Sources (no improvisation; "ref N" = page N of the Parasolid XT Format Reference, Oct 2006, a free copy kept in
 * ~/mastercam-re/work/occt/ref/Parasolid-XT-format-reference-2006.pdf):
 *   - node field lists / order / encodings: the schema Mastercam installs, ~/mastercam-re/files/sch_26105.s_t (read at
 *     run time with xtocc::load_schema, so every node is written with exactly the transmitted fields of its schema
 *     entry); field encodings formats/xt.md + ref 7-8, 16-17 (binary; pointer = index + 1, big-index pair >= 32767);
 *   - text header: copied byte for byte from the 108 SOLIDXMT blobs in the user's parts (all identical apart from
 *     KEY / FILE / DATE: survey ~/mastercam-re/work/occt/xtw/survey2.py);
 *   - every constant field value (BODY res_size / res_linear / state / body_type / nom_geom_state, null-double
 *     tolerances, LIST / POINTER_LIS_BLOCK layout, the CNC/mcEptrPtr ATTRIB_DEF and its ATTRIBUTE on every face and
 *     edge, user field 0 on every node, the trailing index after the terminator): the field survey over the same 108
 *     solids (~/mastercam-re/work/occt/xtw/survey.txt), cross-checked with ref 85-107;
 *   - topology rules: ref 18-25 and 85-95 (fin vertex = forward vertex; EDGE.fin = the positive fin; loops keep the
 *     face on their left seen from the face normal; FACE.shell = back shell, front_shell = front shell; body.region =
 *     the infinite void region; SHELL.body null for void shells), all confirmed on the 108 solids (survey3.py);
 *   - geometry parametrisations: ref 29-33 (LINE, CIRCLE, ELLIPSE, B_CURVE), 52-58 (PLANE, CYLINDER, CONE, SPHERE,
 *     TORUS), 65-70 (B_SURFACE), 78 (senses).
 * Ours (labelled): node numbering / node_ids (any distinct values are legal, ref 23 / 87), chain orders (unordered,
 * ref 3), surface / curve sense always '+' with the orientation carried by FACE.sense (legal: ref 78), one curve per
 * edge and one surface per face (no sharing), periodic -> Parasolid periodic B-curve conversion, refusal of anything
 * that cannot be written exactly.
 * Decoded from Mastercam-written XT in the internet sample parts (<mastercam-samples>/net,
 * scripts ~/mastercam-re/work/occt/netx/): CONE sign (+ v A, against ref 55), B_SURFACE vertex order (u-major),
 * CURVE_DATA always present.
 * Refused (Report::unmapped names it): toroidal apple / lemon surfaces (major <= minor); offset curves / surfaces;
 * tolerant edges (SP-curves); SURFACE_DATA is not written (optional, ref 68 - Parasolid writes one, see occt_xt.md). */
#ifndef OCCT_XT_H
#define OCCT_XT_H
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <TopoDS_Shape.hxx>
#include <TopTools_DataMapOfShapeInteger.hxx>
#include "xt_occt.h"

namespace occxt {

struct Options {
    double scale = 1000;                     /* model units per metre (1000 = mm, 39.37007874015748 = inch) */
    int pk = 27;                             /* Parasolid transmit version: 27 = X9 (SCH_2700142_26105), 28 = Mastercam 2017 (SCH_2800143_28002) */
    std::string key = "sld_1";               /* header KEY= (Mastercam writes the SOLIDXMT name, e.g. sld_378) */
    std::string date;                        /* header DATE= (asctime form "Sat Jun 01 09:06:29 2024"); empty = now */
    bool mc_attributes = true;               /* the CNC/mcEptrPtr attribute on every face and edge (as Mastercam) */
    TopTools_DataMapOfShapeInteger face_color;  /* optional per-face Mastercam colour -> CNC/mcColor attribute */
    /* ours, optional: node_ids to keep for faces / edges / vertices (by sub-shape), e.g. the ids read from a stored XT
       so a regenerated body keeps them; other nodes get fresh ids above the largest one given */
    TopTools_DataMapOfShapeInteger node_id;     /* keyed by shape (TShape + Location; orientation ignored) */
};

struct Report {
    std::map<std::string, int> counts;       /* written node type -> count */
    std::vector<std::string> unmapped;       /* geometry that cannot be written exactly (body refused) */
    std::vector<std::string> notes;
    int seams_dropped = 0, degenerate_dropped = 0, ring_edges = 0;
    double max_occ_tolerance = 0;            /* largest OCCT vertex / edge tolerance met (model units) */
    std::string error;
};

/* shape: a solid, or a compound of solids (each solid -> one solid region of one body). */
bool write(const TopoDS_Shape &shape, const xtocc::Schema &s, const Options &o, std::vector<uint8_t> &out, Report &r);

}  // namespace occxt
#endif
