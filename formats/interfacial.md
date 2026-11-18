# Interfacial kernel (Stock Model, Silhouette Boundary, Turn Profile) - decompile log

Started 2026-11-11 (user decisions N9-6, N9-24, N11-4). Goal: decompile the closed mesh kernel Mastercam 2017 uses for
Stock Model generation (op 107), rest material from stock, Silhouette Boundary and Turn Profile, then port the paths
Mastercam actually calls. Out of scope (NOTES.md goals): the licence lock `interfacial08.dll` (class `Modulock`) and
`GenerateInterfacialSecurity` / `Surfer::ReadEncryptedFile` - only how far the kernel's results depend on them is noted.

Files: `~/mastercam-re/files/` (Mastercam 2017 install, all dated 2016-05-27). Ghidra project `~/mastercam-re/ghidra_if/`
name `ifk` (all 31 DLLs, full auto-analysis; import log `~/mastercam-re/logs/ghidra_if_import.log`).

## 1. Layering (from the PE import / export tables, objdump -p)

```
mckernel / mcmill / mcmsurf / mclathe   (LoadLibrary, no static import of tpiftools)
  -> tpiftools.dll   ComputeMC* classes, UIDlgComputeMC, MCTool / MCConvexTool / MCCustomTool / MCUndercutTool
       -> IF*.dll    one thin wrapper per operation (MastercamUIDialog in the constructor)
            -> interfacial01..30.dll   the kernel, one C++ class per DLL (exports keep MSVC names)
                 -> interfacial08.dll  Modulock (licence lock, out of scope)
```

### 1.1 tpiftools.dll (3.3 MB .text, entropy 4.6: large tables in .text; 362 exports)
ComputeMC (base, 49 exports), ComputeMCLevel0 / Level1, ComputeMCSweepBase / ComputeMCSweep (Stock Model; worker count
min(3, CPUs), stock_model.md), ComputeMCStockCompare, ComputeMCOffset, ComputeMCZCut, ComputeMCUnderOffset,
ComputeMCUncut, ComputeMCStitch, ComputeMCHeal, ComputeMCSilhouette, ComputeMCSpinsil (Turn Profile; takes an
`IF::IffBinKurvFormat`), ComputeMCBitangent, ComputeMCSphoffset; tool classes MCTool, MCConvexTool, MCCustomTool,
MCUndercutTool; UbvParams / UbvOpParams (the UBVS toolpath files of stock_model.md).

### 1.2 IF wrappers -> kernel DLLs
| wrapper | exports | kernel DLLs used (iNN = interfacialNN.dll) |
|---|---|---|
| ifsweep | IFSweep (18): SetTool, SetUnderTool, SetCustomTool, Define{Line,Arc}CustomToolPiece, Open3AxisInputChannel, New/End5AxisToolPath, Add5AxisPoint/Edge, OpenStockChannel, OpenStockNormalsChannel, OpenCompareSurfaceChannel, Run (2 overloads), Compare, SetProcessorCount, SetFiveAxisBufferingLevel | i01 i02 i04 i09 i10 i16 i20 i28 |
| ifsilhouette | IFSilhouette | i11 |
| ifspinsil | IFSpinsil (2) | i02 i08; has two extra sections `text` 1k / `data` 7k = the CRT's SSE2 `log` (FUN_180065000, used for the quadtree depth) and its table (checked 2026-11-11) |
| ifzcut | IFZCut (5) | i01 i02 i03 i04 i08 |
| ifoffset | IFOffset (4) | i01 i04 i05 i08 i20 |
| ifunderoffset | IFUnderOffset (5) | i01 i14 |
| ifuncut | IFUncut (5) | i12 |
| ifstitch | IFStitch (3) | i01 i02 i08 |
| ifheal | IFHeal | i01 i09 i10 i30 |
| ifbitangent | IFBitangent | i01 i13 |
| ifsphoffset | IFSphoffset (3) | i16 |

### 1.3 Kernel classes (one per DLL; "Buffered" = a queued-input twin of the same class)
| DLL | class | .text | uses |
|---|---|---|---|
| i01 | Surfer / SurferBuffered (mesh container + I/O, ReadEncryptedFile) | 325k | - |
| i02 | Stitch / StitchBuffered | 263k | i08 |
| i03 | PlaneCut / PlaneCutBase / PlaneCutBuffered | 405k | - |
| i04 | ToolHead / ToolHeadBase / ToolHeadBuffered (tool shape, Z filter) | 492k | - |
| i05 | Trip / TripBuffered | 518k | - |
| i06 | SurfSub / SurfSubBuffered | 544k | i01 |
| i07 | Shath / ShathBuffered | 2066k | i01 |
| i08 | Modulock (licence; out of scope) | 1630k, entropy 5.35 | - |
| i09 | Compress / CompressBuffered (mesh decimation + normals) | 275k | - |
| i10 | Boolean (Union / Subtract / FixTrackMismatch) | 318k | i01 i02 i04 i05 i06 i08 i22 |
| i11 | Silhouette | 1793k | i01 i02 i03 i05 i08 |
| i12 | Uncut | 214k | i01 i04 i05 i06 i08 i09 i11 |
| i13 | Bitangent | 386k | i01 i04 i05 i06 i08 i09 |
| i14 | UnderOffset | 206k | i01 i02 i04 i05 i08 i10 |
| i16 | Sphoffset | 313k | i01 i04 i05 i06 i07 i08 i09 i10 i20 |
| i20 | Facet | 330k | i01 i06 i07 i08 |
| i22 | IFEqualize | 218k | i08 |
| i28 | Sweep / SweepManager (the swept-volume engine) | 496k | i01 i02 i03 i04 i05 i06 i08 i09 i10 i11 |
| i30 | Heal | 257k | i02 i03 i05 i07 i08 i09 i16 i22 |

Sections of every kernel DLL have code entropy ~6.4: compiled code, not packed or encrypted (checked 2026-11-11).

## 2. Scope by feature (kernel DLLs to decompile, from 1.2 / 1.3)
- **Stock Model (op 107)**: tpiftools ComputeMCSweep(Base) + ComputeMCStockCompare -> ifsweep -> i28 Sweep, i04 ToolHead,
  i10 Boolean, i09 Compress, i02 Stitch, i01 Surfer, i16 Sphoffset, i20 Facet (+ their deps i03 i05 i06 i07 i11 i22).
  That is most of the kernel (about 9 MB of code).
- **Silhouette Boundary**: ComputeMCSilhouette -> ifsilhouette -> i11 Silhouette (+ i01 i02 i03 i05).
- **Turn Profile**: ComputeMCSpinsil -> ifspinsil -> i02 Stitch (+ its own code; i08).

Suggested order (smallest closed path first): Turn Profile, Silhouette Boundary, then Stock Model.

## 3. Call path and parameter passing (decoded 2026-11-11)

Silhouette Boundary, end to end:
1. mckernel `TPSilhouette::Compute` (decomp/mckernel_all.c l.229004): working directory, log `silhouette_log.txt`,
   `UIDlgComputeMC` message handler; `ComputeMCSilhouette(dlg, workdir, in_path (this+0x20), out_path (this+0x28),
   tol (this+0x68), flag)`; `ComputeMC::EnableDataTransform(p_3d, p_3d)`; `ComputeMC::RunComputations`; ok = return 0.
   `TPSilhouette::SetSilhouetteTolerance` stores the tolerance at +0x68.
2. tpiftools `ComputeMCSilhouette::InitializeProcess` (18031c050) makes an `IFSilhouette` (this+0x6f8), then
   `ComputeMC::StandardInitialization`; `runIfModule` (18031c130) = `IFSilhouette::Run(in (this+0x4b4), out (this+0x5b8),
   tol x scale (this+0x18 x this+0x10), dlg, this+0x68, &ComputeMC::m_security, this+0xcc)`.
3. ifsilhouette `IFSilhouette::Run` (1800264f0): packs a parameter blob = string param_7, in path, out path, bytes 1, 1,
   double tol, byte 1, 8 zero bytes, double 90.0 (0x4056800000000000), byte 0, string param_5; maps every byte through a
   256-byte table the `Silhouette` constructor fills (obfuscation), copies 500 bytes of `m_security` into the
   `SilhouetteTransfer`, calls `Silhouette::Run(blob, len, transfer, false)`, then post-processes the output file
   (FUN_180026a40; "Failed to postprocess output.").
4. interfacial11 `Silhouette::Run` (18001f1c0, 873 lines): un-maps the blob with the table at this+8 +0x100, reads the
   fields back into thread-local state (TlsGetValue on every global), strings "Reading the input...", "Preprocessing the
   input", "Stitch2D error 1: memory leak.", "Access denied." (licence path, out of scope). Uses PlaneCutIO / Stitch2D:
   interfacial11 links its own copies of other kernel parts, which is why it is 1.8 MB.

The byte table and m_security are the licence layer (out of scope); a port reads the decoded fields directly.

## 3b. The Mastercam-side pipeline (decoded 2026-11-11)

Callers: Wireframe Silhouette Boundary / Turn Profile -> mcmsurf `mucbMakeIffSiluetBndy(hmmSiluetBndyDat*, CHAIN**, char,
char, bool)` (180231800, mcmsurf_all.c l.367893). Also from mcmsurf `mucbMakeIffLatheStockProfile` (l.367574) and
`mucbMakeIffLatheStockProfileFromStockModel` (l.367738), both at tol x 0.5 and format 0, and mcmill run_mctutl_silbndy
(mcmill_all.c l.455262, 483070, 483183 with tol x 0.1).
mccore exports (decomp/interfacial/mccore_spinsil_bndy.c, MSurfPl2.cpp): `run_mctutl_silbndy(in, out, tol, box_lo, box_hi)`
= runIFKernelSilBndy (180597ec0) and `runMCtutlSpinSilBndy(in, IffBinKurvFormat, out, tol, box_lo, box_hi)` =
runIFKernelSpinsil (180595e80). Each builds mccore's MCSilhouette / MCSpinSilhouette (TPSilhouette / TPSpinSilhouette
with an MCHSTMessageHandler), sets input / output file, bounding box, tolerance (Spinsil: also the curve format,
0 -> 0, 1 -> 1, else 2), Compute(). `run_mctutl_stitch` = runIFKernelStitch (180594780).

mucbMakeIffSiluetBndy steps (temp directory files):
1. `wr_stl(tol = dat+0x10, ...)` writes the selected geometry to `SILBNDRY.STL`. The geometry is the selection, and the
   tessellation is wr_stl's.
2. Active view not Top (GetActiveViewNo != 1): `mucbTransformStl` -> `SILBNDRYSX.STL`, which is used instead.
3. FUN_18022feb0 (box of the STL into box_lo / box_hi); `ConvertStlFileToBinIFF(stl, IFFSILTRIANGLS.TMP, 1.0, 0)`.
4. spin = dat[0x4d] (Turn Profile). Stitch when spin and FUN_18021f7c0 (the database has an entity of mask 0x100100,
   get_ent type 4): `run_mctutl_stitch(IFFSILTRIANGLS, IFFSTITCHTRIANGLS.TMP, 0, alloc_nc_size, tol x 0.5, box)`; else
   (or on failure) the triangle file is copied.
5. Kernel at tol x 0.5 into `IFFSILBNDYS.TMP`: Silhouette Boundary = run_mctutl_silbndy; Turn Profile =
   runMCtutlSpinSilBndy with format = (spin == 0) = 0, after FUN_1802339f0 (when dat[0x55]: the axis data dat+0x18..0x47
   applied to the triangles -> `IFFSILTRISRX.TMP`).
6. FUN_18020c4f0 reads the IFF curves and writes `HBESILBNDYS.TMP`; depth: mucbConstrDepthWcsOrigin, or FUN_18021ef60
   when dat[0x4f]; dat[0x55]: FUN_1802340e0 back-transform -> `HBESILBNDYSRX.TMP`; FUN_180232540(file, single = !spin ||
   !dat[0x55], 0, 1.0) makes the chains.

## 3c. File formats

**Binary IFF surface** (`ConvertStlFileToBinIFF(stl, out, scale, flag)`, mccore 1806cdb90): header = the 14 bytes
`binary surface` (no NUL), int32 node count, int32 triangle count (written as 0 first, rewritten at the end); then per STL
triangle 3 nodes x 3 doubles (72 bytes, coordinates x scale; nodes are NOT shared, node count = 3 x triangles);
then per triangle int32 i, i+1, i+2 (1-based node numbers) + 1 byte = flag.

**Binary IFF curves** (reader in mcmsurf: FUN_180212b30 header, FUN_18021d5d0 curve, FUN_18021d760 / FUN_18021d7b0
segments): header 13 bytes (format 0) or 18 bytes (format 1), only the first byte is checked (`b`); int32 node count
(+0x134), int32 (+0x130); nodes 24 bytes each (3 doubles). Then per curve: int32 segment count (+0x140), and for format 1
two more int32 (+0x144, +0x148); per segment two int32 node numbers, 1-based (made 0-based on read). The header strings are
`binary curves` / `binary tree_curves` (writer CurveXferMC, section 6.1).

## 4. Size of the algorithms (static call graph from the dumps; virtual calls not followed, so lower bounds)
| entry | reachable functions | lines | without CRT / MFC |
|---|---|---|---|
| interfacial11 `Silhouette::Run` | 234 | 15,683 | 164 functions, 12,589 lines |
| ifspinsil `IFSpinsil::Run` (+ i02 Stitch imports) | 417 | 14,479 | 340 functions, 10,380 lines |

With calls through data followed (tools/ghidra_scripts/DataRefGraph.java: every slot of every function-pointer table a
function references, so an upper bound; graphs in decomp/interfacial/*.graph.tsv):
| entry | reachable functions | lines | without CRT / MFC names |
|---|---|---|---|
| interfacial11 `Silhouette::Run` | 633 | 36,757 | 522 functions, 31,937 lines |
| ifspinsil `IFSpinsil::Run` | 753 | 22,607 | 606 functions, 15,952 lines |
| interfacial02 `Stitch::Run` (used by Turn Profile and step 4 of 3b) | 244 | 11,514 | 158 functions, 8,141 lines |
So Turn Profile is about 24k lines of kernel and Silhouette Boundary about 32k (both still include some unnamed MFC
code in the FUN_ counts).

Dumps (decomp/ is not in git): `decomp/interfacial/ifspinsil_all.c` (53k lines), `i02_stitch_all.c` (62k),
`ifsilhouette_all.c` (38k), `i11_silhouette_all.c` (400k), `tpif_spinsil_silhouette.c` (tpiftools path, 2.7k).

## 6. Turn Profile kernel: IFSpinsil::Run (ifspinsil 1800401a0, decomp/interfacial/ifspinsil_all.c) - in progress

`IFSpinsil::Run(CurveTransfer* out, double tol)`:
1. FUN_180040990(dlg, this+8, len, this+0x108) = the licence check ("Access denied."; out of scope).
2. `EdgeSet` (FUN_180039460, vftable 18004b268; a StitchIO): node / edge / triangle lists (this+8 / +0x10 / +0x18),
   memory pools of 1000, merge epsilon 9.5367431640625e-07 = 2^-20. `Stitch::Run(tol, "Preprocessing", false)`
   (interfacial02) stitches the input triangles and feeds them into the EdgeSet through its slots:
   - slot 1 (AddNode, FUN_1800385c0): node p (3 doubles) is kept and mapped to the (axial, radial) plane:
     **(p.x, sqrt(p.y^2 + p.z^2))**, radial < 1e-7 -> 0; FUN_180037a60 merges it into the 2D vertex set. The spin
     axis is the X axis of the coordinates mcmsurf hands over (its FUN_1802339f0 transform, section 3b step 5).
   - slot 3 (AddTriangle, FUN_180037d50): triangle record (3 node pointers); FUN_18000fdf0 builds the triangle's
     edge directions / unit normal and its dominant axis; FUN_18000f800 intersects the axis line (origin, direction
     (1, 0, 0); the origin DAT_18005ebd8 is in .bss = 0) with the triangle plane, FUN_180010050 tests the hit inside
     the triangle (tolerance 1e-7). Axis pierces it: the hit becomes a node (slot 1) and the triangle is split in
     three (FUN_180037b30 queues sub-triangles). Then per queued triangle: an edge whose closest approach to the axis
     is interior (FUN_180037bb0: FUN_18000fcb0 line-line closest point, parameter not within 1e-7 of 0 or 1) is split
     there (new node), so every edge is monotone in radius, and the two halves are queued again. Otherwise the
     triangle's three (x, r) corners give the 2D orientation (FUN_18000f7a0 = cross product with an error bound,
     FUN_180013fe0; FUN_1800042a0: degenerate when |cross| <= bound x 2.220446049250313e-16): degenerate -> its 3 edges
     added unflagged; cross >= 0 -> edges a-b, b-c, c-a flagged; < 0 -> b-a, c-b, a-c flagged.
   - FUN_1800378d0 (add edge): key = the node pair in pointer order (FUN_180037450), deduplicated; flag bit 1 = seen
     from its first node, bit 2 = from its second; flag 3 = seen both ways = interior (its triangle list is freed);
     otherwise the triangle is appended to the edge's list.
   - slot 5 (end of input, FUN_180038ae0): every triangle on an edge with flag != 3 is visited once and FUN_180038690
     adds its inner outline: x range [min, max] of its corners (skipped if within 1e-5); recursive bisection in x:
     the radius at the middle x is FUN_1800376a0 = the **minimum** radius of the triangle's cross-section at that x
     (the section segment from FUN_180037540 on each edge, then min(|p|, |q|, the segment's closest approach to the axis
     when interior)); if the squared distance of (mid, r) to the chord (FUN_18000fb00, point-segment) is <= EdgeSet+0x60
     (the tolerance), the chord is added as an edge (unflagged), else the piece is split at (mid, r).
     So each triangle contributes its region [r_min(x), r_max(x)]: the straight outer edges plus the sampled curve of
     its closest approach to the axis.
   - 2D vertices (FUN_180037a60): x and r are snapped DOWN to the grid 2^-20 (FUN_180004420 = floor(v / g) x g,
     FUN_180041458 = CRT floor) and deduplicated.
   - Vector helpers (all checked): f250 a-b, f2c0 a+b, f330 s x a, f410 cross, f4c0 dot, f670 normalise (0 stays 0),
     FUN_18004155c sqrt, FUN_180004320 |a-b| <= tol, FUN_18000f510 / f570 "<= 0 + tol" / ">= 1 - tol".
3. A 2D arrangement, class **Shell2D** (FUN_18003dad0(eps = 2^-20) stores eps^2 = 2^-40 (its grid g), eps^4 = g^2
   and eps^6 = g^3, *param_1 = param_2 * param_2 (corrected 2026-11-11); the same class with the same
   error strings 1-10 is in interfacial11, so it is shared with Silhouette Boundary): every edge of type != 3 is
   inserted with its end vertices (FUN_18003bce0(x, y): snapped to eps, cached in node[2]; tracks the extreme vertex
   via FUN_18003b890; FUN_18003ab80 inserts the segment unless both ends are equal), FUN_18003c000; then the cached
   vertices are cleared and every edge is inserted again **mirrored (x, -y)**, FUN_18003c000.
4. "Spinning" = Shell2D boundary extraction FUN_18003e860:
   - FUN_18003e370: bounding box of all vertices; a node index with tolerance 1e-7 (0x3e7ad7f29abcaf48); every segment
     goes into a **Shell2D::EdgeQuadtree** (FUN_1800049c0, vftable 18004afb0) over the square box of side
     max(dx, dy) (FUN_180005790; FUN_1800043f0 = max). Smallest cell (FUN_180009400) = side / 2^(k + 1),
     k = (int) ceil(log n / log 4) with n = edge count (FUN_180065000 = log, FUN_180040d40 = CRT ceil, FUN_180040e60 =
     pow; checked 2026-11-11). Traversal FUN_180005820 is iterative (child index at node+0x22: 0-3 children, 4 = leaf, < 0 =
     free); a cell is split (FUN_180009180) when slot 2 says its size (node+0x40) > the smallest cell: four children
     of half size (child 0 = low corner, 1 = +h +h, 2 = +h x, 3 = +h y), the edges distributed by FUN_18000d870 (x
     split, then y splits); slot 3 (FUN_180005c30) flags the children for a further pass when any child holds more
     than half of the parent's edges. A leaf with >= 2 edges (slot 0, FUN_18003aca0) becomes a **candidate group**:
     a list appended to Shell2D+0x48, and each edge records the group (edge+0x20). The quadtree does not intersect. It only proposes candidate pairs: an edge goes into every cell it reaches (FUN_18000d870: the QTreeEdge classifier, vslot 0, returns < 0 = low side, > 0 = moved to the high side, 0 = straddles -> kept low and a clone added high), so two
     crossing edges always share a leaf, and the depth (even a log rounding at n = 4^k) cannot change the result.
   - Crossings are computed lazily during the walk: FUN_18003cd90(edge) intersects the edge with every edge of its
     groups (stamp per pass so each pair is tested once, DAT_18005eb38), FUN_18003c5e0 per pair; the hit vertices
     collected in edge+0x18 are sorted along the edge's major axis (FUN_18003a1d0) and the edge is replaced by
     sub-edges (FUN_18003a9e0); one hit = two sub-edges directly.
   - FUN_18003c5e0 (pair test): orientation of each end of B against A's line with a certainty flag (FUN_180039eb0,
     filtered); an end that is A's own end counts as certain; FUN_18003bb30 rejects / classifies from the two signs
     (touching at an existing vertex -> that vertex is used, FUN_18003a3c0 / FUN_18003a440 merge); then the same with
     A against B; if any sign is uncertain the exact predicate FUN_18003b370 is used for both. A proper crossing makes
     a new vertex = A.p + t (A.d) with t = cross(B, A.p) / cross(A.d, B.d) in the filtered arithmetic
     (FUN_180004030 mul, FUN_180003e80 sub, FUN_180004180 div, FUN_180003fa0 add), unless the denominator is
     uncertain (FUN_1800042a0) -> FUN_18003adb0. With Shell2D+0x68 set, crossings of the same two input edges are
     shared through a map (+0x70).
   - **Filtered arithmetic** (all decoded): a number is (v, e), the true value within e x 2^-53 of v. From a double:
     e = 0 (FUN_18003fe40). a + b, a - b: e = ea + eb + |v|. a x b: e = |b| ea + |a| eb + ea eb 2^-53 + |v|.
     a / b: e = (ea + |v| eb) / (|b| - eb 2^-52) + |v|. Products of plain doubles (FUN_18000f390): e = |v|.
     "Certainly zero" test FUN_1800042a0: |v| <= e x 2^-52.
   - Predicates: a line record (FUN_18003c040) = base vertex + direction (dx, dy) as plain differences (FUN_180003e00:
     e = |v|), base = the edge's lexicographically lower end by FUN_18003be10 (x then y; equal ends = "Shell2D error
     8: invalid edge."). Orientation FUN_180039eb0(line, P) = (P.x - b.x) dy - (P.y - b.y) dx in the filtered
     arithmetic (a computed vertex brings its own error, FUN_180003f10); returns +1 / -1 when certain, 0 with
     certain = (e == 0) when |v| <= e x 2^-52. FUN_18003bb30 classifies a pair from the two end signs: both 0 =
     collinear overlap (each end tested against the other edge, FUN_18003ba10); one 0 = touching at that end (kept
     only when it is a shared vertex); same non-zero signs = no crossing.
   - **Exact layer** (used when a filtered sign is uncertain, FUN_18003b370, and to build every crossing vertex,
     FUN_18003adb0): sign-magnitude big integers, 1 sign byte (1 = positive; 0 for v <= 0) + 32-bit limbs, least
     significant first (+4); FUN_18000e850 add (FUN_18000ed50; the second operand's sign is flipped when its flag
     byte is 0, i.e. it subtracts), FUN_18000eac0 multiply (FUN_1800045b0 limb x scalar), FUN_18000e8b0 sign,
     FUN_18000e980 negate. Input vertex -> 2 limbs (FUN_180004f20: q = v / g, an integer since the vertices are on the 2^-20 grid;
     low limb = q - floor(q / 2^32) 2^32, high = floor(q / 2^32); >= 2^32 sets the overflow flag -> "Shell2D error 4").
     Crossing of A (a0, a1) and B (b0, b1): den = (a1 - a0) x (b1 - b0) (4 limbs, eps^2 units; 0 = "Shell2D error 2:
     bad intersection."), num = (a0 - b0) x (b1 - b0); x = a0.x den + num (a1.x - a0.x) (6 limbs, eps^3 units), y the
     same; den < 0 -> FUN_18000e980. Back to doubles: FUN_180004cc0(big, n, scale) = sum over limbs of (double)limb x
     scale x 2^(32 i) (limb as unsigned via cvtsi2sd; disassembly 180004cc0, the decompile lost the loop), negated when
     the sign byte is 0; scale = g^3 = 2^-120 for x / y, g^2 = 2^-80 for den (Shell2D +0x10 / +0x8); input
     vertices are integers in units of g = 2^-40 (FUN_180004f20 with *Shell2D). The vertex = filtered
     (x / den, y / den) with e = |v| from FUN_18003fdd0 before the division. Any exact implementation gives the same
     signs; only these two conversions must be bit-exact.
   - Vertex merging is union-find: FUN_180039af0 = find with path compression (vertex+0x38 = parent).
   - Components and seeds: FUN_18003ac60 opens a component (list +0x58 / current +0x60) before each insertion pass
     and FUN_18003c000 closes it, so the original edges and the mirrored edges are two components. FUN_18003bce0 keeps
     each component's extreme vertex: comparator FUN_18003b890(a, b) on the global axis DAT_18005eb20 (set to 0 = x
     there): filtered compare of coordinate [axis] (FUN_1800043b0 = intervals overlap?), else plain compare
     (b <= a -> 1, else -1), overlapping with both errors 0 -> 0, else exact FUN_18003b670. A new vertex replaces the
     extreme when the result is 1, so the extreme is the **maximum x**, the first one kept on exact ties.
   - Walk FUN_18003e860, per component not yet done (component+8): seed FUN_18003d4c0: the edges in the quadtree
     groups of the extreme vertex that pass through it (FUN_18003c120, stamp DAT_18005eb38) are candidates; the seed is
     the first candidate e such that the ends of all other candidates lie on one side of e (exact orientation
     FUN_18003b610 = filtered, exact when uncertain; sides collected as bits 1 / 2, rejected at 3); none = "Shell2D
     error 6: unable to find seed edge."; side bit 1 -> the edge is reversed (FUN_180039a20); all collinear (bits 0):
     vertical (equal x) -> axis y and reversed when its first end is greater, else bit 2. The seed is split at its
     crossings first (FUN_18003cd90); the step stack (param_3) holds (edge, vertex) pairs; the seed is marked visited
     (+0x28) and appended to the output list +0x40.
   - Step at vertex v arriving over edge E (FUN_18003e860 loop): the incident edges of v (vertex+0x48) and those of
     their groups are split at their crossings first (twice, FUN_18003cd90); the far vertex of E; then each incident
     edge != E not already used in that direction (FUN_180039b50: visited and its [dir] end resolves to v) is a
     candidate; the first becomes the incumbent (side class from FUN_18003b610 of its far end against E's line,
     unless it lies on the same input edge (+0x10) = 0, then FUN_18003b980: same order along the axis =
     straight-on), and every further candidate goes through FUN_18003c2f0: side of the candidate's far end against
     the incumbent's line (0 when both come from the same input edge); 0 -> straight on / back by FUN_18003b980; < 0
     or > 0 replace the incumbent by the side class and the tie-break FUN_18003c230 (side of the incumbent's far end
     against the candidate's line, negated when the incumbent's [dir] end is v), preferring unvisited edges on ties.
     The chosen edge is marked, appended to +0x40 and pushed with its far vertex; reversed (FUN_180039a20) when it
     points into v. No candidate -> the result flag (local_170) = 0 = the "cracked periphery" error. Progress through
     the dialog every 17 steps ("Spinning...").
   - failure = "IFSpinsil error 2: cracked periphery." (the run continues and outputs what it has).
5. Output through CurveTransfer: slot 0 (begin), slot 0x18 (add node (x, node+0x10)) once per vertex, slot 0x20 (add
   segment), slot 0x28 (end), for each boundary segment in the list at arrangement+0x40.

   - Small helpers (decoded): FUN_18003ba10 (collinear pair): an end equal to an end of the other edge merges the two
     vertices (union, FUN_18003a440); an end strictly between the other's ends (FUN_18003b890 signs differ) is added
     to that edge's hit list. FUN_18003a9e0 makes a sub-edge (input edge id kept at +0x10; with +0x68 shared through a
     map). FUN_18003c120 (vertex on edge): an end, or collinear (exact orientation 0) and not outside both ends.
     FUN_180039a20 reverses an edge (swap ends, toggle +0x29). FUN_1800043b0 = "possibly equal" (difference certainly
     zero test); FUN_18003b670 = exact coordinate compare of two vertices (input x den vs crossing numerator, or two
     crossings cross-multiplied, 6 / 10 limbs).

### 6.1 The tpiftools data transform around the kernel (ComputeMC, decoded 2026-11-11)
ComputeMCSpinsil::runIfModule (18031c940) = `IFSpinsil::Run(curves_out (this+0x708), tol (this+0x18) x scale
(this+0x10))`; the same tolerance is the Stitch merge radius and EdgeSet+0x60 (the inner-curve bisection limit; the
EdgeSet constructor stores Run's tolerance at +0x60). The data are normalised first (mckernel calls
ComputeMC::EnableDataTransform with the bounding box set by SetBoundingBox):
- translation, in ComputeMC::CheckDataLocation (180315190): t = getTransVec (1803160d0): per axis, c = (box_lo +
  box_hi) / 2 (box at +0x38..0x60); when |c| > 20 (500 when +0x6ed is set) the component is -c, else 0. Only if any
  component is non-zero: **t.y += 0.04 and t.x += 5 x 0.04 = 0.2** (0.1 / 0.5 when +0x6ed), then vslot 0x18
  (ComputeMCSpinsil::adjustTransVec 18031c9a0: t.y = t.z = 0, so only x moves and the axis stays), t stored at
  +0x20..0x30, applied by vslot 0x38, flag +0x6f0.
- scale, in ComputeMC::CheckDataSize (180315040; dump decomp/interfacial/tpif_transform.c): defaults (setProcessDefaults
  1803160a0) scale 1.0, t = 0, flags clear. vslot 0x28 = checkToleranceValues (180315010): p = 5e-5 / tol, ok = tol
  >= 5e-5. Not ok -> scale = p (tol x scale = 5e-5); ok -> GetScaleFactor (1803161b0; only when an extent > 3950:
  s = 3950 / (m + dataSizeAdjustment() = 0.0), m = the largest |extent|) and s is used when p < s; any chosen
  value != 1 is stored at +0x18 and applied by vslot 0x30 = ExecDataScaling, flag +0x6ef.
- applying: ExecDataScaling / ExecDataTranslation (180315170 / 1803152f0) rewrite the input binary IFF file in place
  (ScaleBinIFFData 180315310 with IFBinaryFileScaler: v x s; TranslateBinIFFData 180314ee0 with
  IFBinaryFileTranslator: v + t), before the kernel reads it. ComputeMC::ReverseTransformation (180315410) undoes
  them on the output file (scale 1 / s first, then translation -t; STL output through ScaleBinSTLData /
  TranslateBinSTLData; format 'a' = ASCII refused, 0x1c).
- So for a part whose box centre is within 20 of the origin on X and with tol >= 5e-5 and extents <= 3950, the
  kernel sees the coordinates unchanged.
- output: CurveXferMC (FUN_180330350) writes the curve file; header **`binary curves`** (13 bytes, format 0) or
  **`binary tree_curves`** (18 bytes, format 1) (closes the open item of 3c).
- input ComputeMC::TransferBinarySurfaceInput (180314b70): reads the binary IFF surface (seek 14, int32 nodes, int32
  triangles, nodes 24 bytes, triangles 3 x int32 1-based + 1 byte) into the SurfaceTransfer (slot 0 begin, slot 8 add
  node, slot 0x10 add triangle, slot 0x18 end). The translation and scale are applied to the file before this
  read (next items).

## 7. Stitch (interfacial02), as Turn Profile uses it - decoded 2026-11-11

IFSpinsil::OpenInputChannel makes `Stitch(dlg, name, security)` behind a CArchiveStream (the input SurfaceTransfer);
the triangles of the binary IFF surface reach it as AddNode / AddTriangle (Stitch exports). Mode (Stitch+8) = 0 (the
constructor; SetStitchTargetMode is not called; > 3 = "Stitch error 13"). Run(tol, "Preprocessing", false) ->
FUN_180009d80(impl, tol, text, mode), flag impl+0x58 = false:
1. Pre-pass FUN_18000b3a0 (runs when mode != 1): every node pinned (+0x38 = mode != 2); each input edge (node pair,
   pointer order, balanced tree) counted over the triangles; an edge used by fewer than 2 triangles (mode 3: != 2)
   marks its nodes as boundary (+0x3a) and, in modes 0 / 3, unpinned (mode 2: pinned). Nodes that were pinned before
   the pre-pass are pinned again at its end. So in mode 0 **only nodes on open edges may move**.
2. InputNodeOctree (FUN_18000d4e0, vftable InputNodeOctree) over the box of all nodes grown by 5 x tol on each side
   (cube of side max extent + 10 x tol); the unpinned nodes are its objects.
3. Node merge FUN_18000a560 for each input node in input order, radius tol (tol^2 compared): an unmapped node becomes a
   representative **at its own position** (no averaging); if it is unpinned, every unmapped, unpinned node in its
   octree neighbourhood within distance^2 <= tol^2 maps to it. Pinned nodes map to themselves. The octree only
   finds candidates; the result does not depend on it.
4. Triangles are remapped to representatives (FUN_18000e840); a triangle with two equal corners is dropped; edges
   (pointer-ordered pairs) are counted in a balanced tree.
5. T-junctions FUN_18000a7a0 for each edge not used by exactly 2 triangles and not flagged: nodes near the edge
   (octree query around the edge, box grown by tol) pass FUN_180005b80: d = B - A, t = ((P - A) . d) / |d|^2 with
   0 < (P - A) . d < |d|^2, foot F = A + t d, kept when |P - F|^2 <= tol^2 and not (t^2 |d|^2 < tol^2 and |P - A|^2 <
   tol^2) and not ((1 - t)^2 |d|^2 < tol^2 and |P - B|^2 < tol^2); kept nodes are listed on the edge.
6. Output FUN_18000b1d0 per triangle: FUN_18000aa60 takes the first edge (0, 1, 2) with listed nodes, sorts them by
   (P - A) . (B - A) ascending and fans the triangle from the opposite corner C: (A, n1, C), (n1, n2, C) ... (nk, B,
   C); the middle triangles are emitted, the first and last are split again on their other edges (up to the three
   edges); a piece with two equal corners is dropped (FUN_18000aea0 returns NULL; 13.3 item 7); FUN_18000b010 emits: new nodes once (StitchIO slot 1, AddNode), edges once (slot 2), the triangle (slot 3),
   edge-to-triangle links (slot 4). The EdgeSet of Turn Profile ignores slots 2 and 4.
Errors seen: "Stitch error 12: memory leak", "Stitch error 5: memory leak", "Octree: memory leak.".


## 8. The port (viewer/ifk, 2026-11-11)

`viewer/ifk/ifk_spinsil.c` / `.h`: `ifk_spinsil_run(mesh, tol, curves)` = IFSpinsil::Run with Stitch (mode 0) and
Shell2D; `ifk_read_bin_surface` / `ifk_write_bin_surface` / `ifk_read_stl` (3c); `ifk_spinsil_transform` (6.1).
`viewer/ifk_cli` (make ifk_cli): `ifk_cli MESH TOL [--svg OUT]`. Self-test `tools/tests/ifk_spinsil_test.sh`
(cylinder 400, cone 200, stepped 350, 24-facet sphere 313.262849 = the inscribed 48-gon, tube: two loops with inner
radius 6 cos(pi/24) = 5.948668, the facets' closest approach): ALL PASS. 180k triangles in about 1 s.
**Checked against Mastercam since 2026-11-12** (N12-3, section 13: the oracle; byte-identical curve files on all cases after the fixes of 13.3). Turn Profile stays greyed in the app (IF-1, mcmsurf side).

Ours (data structures, and orders the original takes from heap addresses), each a possible source of differences
from the DLL in tie cases only:
- [superseded by 13.3 items 2-4: emulated pool addresses, the EdgeQuadtree ported, Shell2D+0x68] Records are arrays; the EdgeSet edge tree and the Stitch edge tree are keyed by vertex pointers (FUN_1800373c0 /
  FUN_180037450): the port uses creation order (pools hand out increasing addresses inside a block of 1000).
- Shell2D EdgeQuadtree groups: replaced by "input edges whose boxes overlap". The pairs that can meet are the same
  (section 6 item 4), the order the pairs are tested in is ours; the seed's candidate order likewise.
- Stitch's octree and InputNodeOctree: replaced by a sweep on x; the merge result does not depend on the query order.
Decoded while porting (corrections to the text above): FUN_18000f880 updates the kept magnitude in its second compare
(no quirk); FUN_180010050 projects with v0 - P; the Shell2D component list is a tree on the extreme vertex's x
iterated from the right (descending; on ties the later component first: for Turn Profile the mirrored half is walked
first); Stitch::AddNode / AddTriangle prepend, TransferToStitch passes no pinning flags, and phase 6 emits in input
order; the T-junction fan splits the end pieces at most once more (FUN_18000b1d0); the vertical-edge merge of slot 5
(edges with equal x are removed, grouped by x, their r-intervals unioned, touching ones merged, each union inserted
twice, FUN_180010740 being a multiset insert); Run writes each output edge's stored ends, not their union roots, so
a merged point can appear as two nodes (seen at the apex of the cone test: (20, 0) and (20, -0)). [Corrected in 13.3 item 1: the walk FUN_18003e860 ends by replacing the output edges' ends with their roots; one node.]
The CurveXferMC file writer is section 9 (ported 2026-11-11). Left: the mcmsurf side (wr_stl, the chains from the HBE
file); the oracle.

## 9. The curve file writer CurveXferMC (tpiftools.dll; decoded and ported 2026-11-11, O3 part 1)

Dumps: decomp/interfacial/tpif_curvexfer.c (FUN_1803310e0 .. FUN_1803312a0, FUN_180330c10 / d60 / ff0 / a30 / b80 / 850 /
750 / 920, FUN_180332190, FUN_180331410), tpif_spinsil_silhouette.c (FUN_180330350 constructor), the vftable from
VtableDump (CurveXferMC::vftable 18033b8b0). Port: viewer/ifk/ifk_curvexfer.c `ifk_write_bin_curves`.

### 9.1 Object and slots [code]
- Made by ComputeMCSpinsil::InitializeProcess (18031c7b0): `FUN_180330350(obj 0xe0 bytes, format = this+0x710, path =
  this+0x5b8)` (the kernel output temp path; CompleteProcess copies it to this+0x3b0 afterwards).
- Constructor FUN_180330350: `fopen(path, "wb")`; format 1 writes the 18 bytes `binary tree_curves`, format 0 the 13
  bytes `binary curves` (no NUL), any other format nothing (and the node limit +0x98 stays 0, so every AddNode fails).
  After the header: index array +0x10 of 60000 int32 (+0x98 = 60000), +0xd8 = 2.5e-7 (0x3e90c6f7a0b5ed8d), counters 0;
  +0xac / +0xb0 = 0; the CMap at +0x60 gets InitHashTable(1000) (FUN_180331650); node growth step +0xa8 = 5000.
- vftable: [0] FUN_1803310e0 begin (node and segment counts +0xb4 / +0xb8 = 0); [1] FUN_1803310f0 (+0xc4++, returns 0)
  and [2] FUN_180331100 (+0xc8++): counters only, never written to the file; [3] FUN_180331110 AddNode(p_3d): stores
  the 3 doubles at +8 (24 bytes each), index[i] = i, returns &index[i]; **i >= 60000 returns NULL** (the index array is
  never grown); [4] FUN_180331200 AddSegment(int* a, int* b, ..): appends (*a + 1, *b + 1) (1-based) to +0x18;
  [5] FUN_1803312a0 end (writes the file, below); [6] FUN_180330550 deleting destructor (fclose).
- IFSpinsil::Run (1800401a0, ifspinsil_all.c) calls slot 0 (begin, arg 0), AddNode with (x, vertex+0x10, 0.0) for each
  vertex on first use (cached in vertex+0x30), AddSegment(a, b, 0, 0) per boundary edge, slot 5. With more than 60000
  nodes AddNode returns NULL, the vertex cache stays 0 and AddSegment dereferences NULL: no defined result (the port
  returns IFK_CX_NODELIMIT and writes no curves).

### 9.2 End: chaining into curves and the file [code]
FUN_1803312a0 writes int32 node count, int32 segment count, the nodes (24 bytes each), then:
1. FUN_180330c10 (returns false at once when there is no segment): for each segment i in order, the record
   (a, b, i, flag 1) (IF::SegIpb) goes through FUN_180330a30: over the lists in CArray order (+0x40, size +0x48), the
   first list where **b == head.a** (AddHead) or else, on the same list, **a == tail.b** (AddTail) takes it; no list
   -> a new CList (block size 4000) appended to the CArray. Lists are never joined to each other here.
2. FUN_180330b80: map[head.a] = list for each list in CArray order (CMap<int, CList*> operator[], FUN_1803320c0: a
   later list with the same head key replaces the earlier one). The hash is a Schrage step of the 16807 generator
   (key mod 127773 x 16807 - key / 127773 x 2836, + 0x7fffffff if negative) mod the table size; it only places
   entries, lookups are by key, so it does not change any result.
3. FUN_180330d60: buffer +0x28 of nseg + 10 int32, -1 for [0, nseg) and the sentinel -999999 (0xfff0bdc1) at nseg.
   Repeat: take the first list (CArray order) whose head record still has its flag; key = its head.a; walk:
   list = map[key] (none -> stop); FUN_180330850 appends the list's segment indices (returns 0 when the next buffer
   slot is the sentinel -> FUN_180330d60 returns 0 -> **no curve at all is written**), clears the flags of the
   list's head and tail records, +0x30 = the first segment's start node (when the buffer was empty), +0x34 = the last
   segment's end node; key = the tail's end node; stop when +0x30 == +0x34. Then FUN_180330750 classifies the
   buffer: fewer than 2 segments -> 4; else the running **sum of squared segment lengths** (not lengths), and at the
   first segment where it exceeds +0xd8 = 2.5e-7 -> 1 if the first segment's start node is the last segment's end
   node, else 0; never exceeded -> 3. Only class 1 is stored (FUN_180330920, curve array of 100 entries, doubled when
   full). **So only closed chains are written; open chains, chains of one segment and tiny loops are dropped.**
   The walk ignores the flags: when the first flagged list is not the one map[its head.a] returns (two lists with the
   same head node), its flag is never cleared and the loop repeats forever (each pass storing the same curve again
   when it is closed). [code: no exit in FUN_180330d60 other than "no flagged list" and the sentinel] The port stops
   there and returns IFK_CX_HANG (-4) after writing header and nodes.
4. Per stored curve: int32 segment count; format 1 also +0xac and +0xb0 (always 0: set in the constructor, no slot
   writes them); then FUN_180330ff0: each segment's two 1-based node numbers (8 bytes), stopping after a segment that
   does not start at the previous segment's end (cannot happen for chains built as above).
No curve count is written: the reader reads curves until EOF. The file is closed by the destructor.

Layout (format 0): `binary curves` | int32 nnode | int32 nseg | nnode x 3 doubles | per curve: int32 n | n x (int32 a,
int32 b), 1-based. Format 1: `binary tree_curves` and two int32 zeros after each n. This matches the mcmsurf reader
(3c; FUN_180212b30: int32 at +0x134 = node count, +0x130 = segment count; FUN_18021d5d0 / FUN_18021d760).

### 9.3 After the kernel: ReverseTransformation on the curve file [code]
ComputeMC::CompleteProcess (1803155e0): when the data transform is enabled (+0x6e8 = 1 or 2; mckernel's
EnableDataTransform(p_3d, p_3d) sets 2) and scaling (+0x6ef) or translation (+0x6f0) was applied, vslot 0x40 =
ReverseTransformation(this+0x5b8); then AnalyzeOutput (180313e00 -> AnalyzeIFFOutputData for 'c' / 't' / 's', NOT
DECODED: it only decides the error code) and copyFile(this+0x5b8 -> this+0x3b0).
ReverseTransformation (180315410): GetDataFileFormat (180313a70; 'b' + byte 7 'c' = 'c', byte 7 't' and byte 12 'c' =
't', byte 7 's' and byte 8 'u' = 's') -> for 'c' / 't' / 's' ScaleBinIFFData(path, s, true) when scaled (factor
**1.0 / s**, multiplied in: FUN_180005950 v = v x f per coordinate), then TranslateBinIFFData(path, t, true) when
translated (the sign bits of t flipped: FUN_180005980 v = -t + v). Both go through FUN_180005690: read 0x1a bytes (a
shorter file fails), header length 14 / 13 / 18 for byte 7 = 's' / 'c' / 't', node count = the int32 after the
header, nodes from header + 8 rewritten in place (in chunks; the chunking does not change the values). Errors 0x1a /
0x1b. Port: `ifk_reverse_transform`, `ifk_bin_iff_scale`, `ifk_bin_iff_translate` (the binary STL branch of
ReverseTransformation is not ported).

### 9.4 Self-test (tools/tests/ifk_curvexfer_test.sh, 2026-11-11)
Hand-made chaining case (tools/tests/ifk_data/cx_chain.txt: a square given out of order = two lists joined by the
map, an open chain, a loop with squared-length sum 4e-8), its exact file bytes (format 0 and 1) and read-back, the
endless-loop case, scale / translate / reverse on a curve file, and the Turn Profile kernel's cylinder written as a
curve file: ALL PASS.
**Finding [settled by the oracle, 13.4: the DLL writes one apex node; port fixed]:** with the current kernel port, the cone and the sphere of
ifk_spinsil_test.sh give curve files with **0 curves**: the kernel port outputs the axis point (apex) as two nodes
((20, 0) and (20, -0), section 8: Run writes the stored edge ends, not the union-find roots), so the chain is open and
FUN_180330750 drops it. The tube and the stepped solid (no point on the axis) give their closed curves. Either the
DLL also outputs two nodes there (then Turn Profile of a pointed part would return nothing, which seems unlikely for
Mastercam) or the port's end of Run differs; to be settled with the oracle (N11-5).

## 10. The Mastercam side of Turn Profile / Silhouette Boundary (mcmsurf / mccore; O3 part 2, 2026-11-11)

Port: viewer/ifk/ifk_mcside.c (file-level, no app wiring); self-test tools/tests/ifk_mcside_test.sh. Dumps:
decomp/mcmsurf_all.c, new decomp/interfacial/mccore_wr_stl.c (wr_stl, surf_to_stl, facet_normal, depth 2).

### 10.1 mucbMakeIffSiluetBndy, details added to 3b [code]
- `wr_stl(tol = dat+0x10, file, "SILBNDRY.STL", -1, 1, 1, 0, param_4, 0)`: entity filter -1 = the selected entities
  (flag 4), **ASCII STL** (param 5 = 1: "solid ascii", `facet normal %f %f %f` / `vertex %f %f %f` of float values,
  so the coordinates reach the kernel rounded to float and then to 6 decimals), quiet (param 6), no view matrix
  (param 7 = 0), param 8 = mucbMakeIffSiluetBndy's 4th argument (entities on invisible levels too), param 9 = 0.
- Active view not Top: mucbTransformStl(0, 0, 1.0, "SILBNDRYSX.STL", in) is a binary STL (below), used instead.
- FUN_18022feb0(stl, box): box of all vertices, start (1e38, 1e38, 1e38) / (-1e38, ...), FUN_180252860 strict
  compares; returns the triangle count, 0 -> the run fails. ConvertStlFileToBinIFF(stl, IFFSILTRIANGLS.TMP, 1.0, 0).
- Stitch only for Turn Profile with an entity of mask 0x100100 (FUN_18021f7c0 = get_ent(type 4, 0x100100): a solid)
  at tol x 0.5; the kernel at tol x 0.5; the converter (10.4) gets +0xa0 = tol x 10.
- Turn Profile with dat[0x55]: FUN_1802339f0(stitched or triangle file, dat+0x18 (two axis points), IFFSILTRISRX.TMP,
  param_5) before the kernel and FUN_1802340e0(HBE file, dat+0x18, HBESILBNDYSRX.TMP) after it; the chains are made
  by FUN_180232540(file, single = !spin || !dat[0x55], 0, 1.0) (not decoded yet).

### 10.2 wr_stl (mccore 1806d2e60) [code]
Loops over the database (get_raw_ent, mask 0x110100) with the filter above, tessellates each surface by surf_to_stl
(1806d22e0) = **make_surf_polygons(1, 0, ent, tol, 0, ..)** (mccore's surface polygoniser) and, for solids (param_4 ==
-1), the solid faces from the solids' surface temp file (rewind_solid_surfs_temp_file / read_next_solid_surf_from_temp_file);
triangle normal = cross product of the polygon corners, flipped (and the corner order 0, 2, 1) when the surface's flag
bit 2 (+0x6e) is set; written as float. With no matching entity: warning 0x153b. **make_surf_polygons and how solids
become surfaces in the temp file are NOT DECODED**: the input mesh of both commands depends on them. Question for the
user in formats/TODO.md (Waiting on the user, IF-1); our own tessellation (OCCT) is not substituted. **2026-11-12 (N12-1): see 12** -
wr_stl / surf_to_stl ported, make_surf_polygons ported for untrimmed NURBS surfaces and matched to the DLL; trimmed
surfaces, other kinds and solids (Parasolid) still open.

### 10.3 Axis and view transforms [code]
- FUN_180232e10(axis, M): d = unit(p1 - p0) (0 when |p1 - p0| = 0); h = (0, 0, 1) when |d.z| < 0.999999 else
  (-1, 0, 0); u = UnitNormal(h, d) = (h x d) / max(|h x d|, ... 1e-12 when smaller) (mccore 1804b7480); w = d x u; M =
  rows d, u, w and origin p0 (an x_matrix).
- FUN_180230670(p, M) = R^T (p - p0) = q.x d + q.y u + q.z w; xform_pt(p, M) (mccore 1808eba20) = R^T p + p0. These
  are **not inverse to each other unless R is symmetric**: the X axis gives the identity, the Z axis takes (0, 0, 5)
  to (-5, 0, 0) and back to (0, 0, -5), and for an axis along (1, 1, 0) a point on the axis goes to the kernel's Y axis
  (ifk_cli --axis; [code], the formulas are read off the decompile; how the dialog fills dat+0x18 is NOT DECODED, so
  whether such axes reach this code is open).
- FUN_1802339f0(in, axis, out, fill) copies the binary IFF surface: 14-byte header; node and triangle counts (both
  must be > 0); with fill: nodes = 9 x triangles, triangles x 3. The node block is read as `triangles` x 72 bytes (so
  the file must have 3 own nodes per triangle: a stitched file with shared nodes is read wrong) and each node goes
  through FUN_180230670; with fill each triangle (a, b, c) is preceded by (pmin, (pmax.x, 0, 0), (pmin.x, 0, 0)) and
  (pmin, pmax, (pmax.x, 0, 0)), pmin / pmax = the corner of smallest / largest x (strict, running compares): the
  region between the facet and the axis. The triangle records (3 int32 + 1 byte) are copied; with fill 2 x n more
  records follow, each the previous one + 3 per index, the last flag byte repeated.
- FUN_1802340e0(in, axis, out): 0x68-byte HBE records until EOF; type (+0x60) 1: p0, p1 through xform_pt; 2: p0, p1,
  p2; other types are not written.
- mucbTransformStl(ascii 0, scale 0, 1.0, out, in) (1802333b0): binary STL with the 80-byte header `MASTERCAM STL DATA`
  + spaces, the triangle count (FUN_18022fd70 counts the input), per triangle every vertex through world_to_view of the
  active view (rows of mcViews[view]: v[i] = row_i . w, decomp/mastercam_acsave.c 180341b30), the normal from
  facet_normal (mccore 1806d0420) normalised again (0 when its length is 0), all as float, attribute 0.

### 10.4 IFF curves -> HBE records (CMutlCvrtIffToHbeFile, FUN_18020c4f0) [code]
Converter (constructor FUN_1802095f0): +0x170 = dat; +0xa0 = 0.254 (metric) / 0.01, +0xa8 = 1.0 (scale), +0x168 =
1000; mucbMakeIffSiluetBndy then sets +8 = curve format, +0x11 = 1 (the filter runs), +0x12 = (spin == 0) (the offset
check runs for Silhouette only), +0xa0 = tol x 10, +0x178 = dat[0x50], +0x17c = int at dat+0x48, +0x180 = double at
dat+0. FUN_18020c4f0: FUN_180212b30 reads the header and nodes (3c), FUN_18021bf50 opens the HBE file; per curve
(FUN_18021d5d0) FUN_180213ee0 builds its records, then (when it has any) FUN_18021ff80 (offset check), FUN_18020de20
(filter, writes its output by FUN_18021f780) or, when that returns false, FUN_18021f730 (raw records).
FUN_180213ee0 per curve: first flag (+0x10) = 1, gap bridging (+0x13) = 1, 2D box reset; per segment FUN_18021d7b0
(two node positions), FUN_18021f540: both points x +0xa8; spin: a y with |y| < |tol| (mccore's system tol) becomes 0,
then **a segment with y < 0 at either end is dropped** (the mirrored half of the Turn Profile curve); FUN_18020dbc0:
when the previous record's end and this start differ by |dx| >= tol with |dy| < tol and |dz| < tol (a gap along x
only), a line record bridges it; the record (type +0x60 = 1, +0x61 = first flag) is added and the first flag cleared.
After the curve FUN_18020c1c0 applies the same test between the last end and the first start (closing record along
the axis). Record bytes other than p0, p1, +0x60, +0x61 are not set (heap memory). Port: `ifk_hbe_records` (before
the filter). For the cylinder of the self-test: 7 records of the upper half + the closing record (0, 0) -> (20, 0).
**Not ported yet**: FUN_18021ff80 (MCCECalcOffset by -|+0xa0| of the curve's boundary; the curve is dropped when the
offset fails), FUN_18020de20 (the filter CMutlFilterHbeAra and, for Turn Profile with dat[0x4f] and !dat[0x53], the
arc fitter CSpinBdryArcFitter), the HBE file written from their output, mucbConstrDepthWcsOrigin / FUN_18021ef60 and
FUN_180232540 (HBE -> chains).

### 10.5 The HBE filter, the offset check and HBE -> chains (decoded 2026-11-11; detail in
decomp/interfacial/notes_mcmsurf_hbe.md, not in git) [code unless marked]
- FUN_18020de20 -> CMutlFilterHbeAra (constructor FUN_1802097b0, really 7 arguments in the disassembly): +0x58 =
  converter +0x178 (dat[0x50]), +0x59 / +0x5a = converter +0x179 / +0x17a (always 0: only the XY plane of FitArc3D
  is reachable), +0x1a2 = (short) converter +0x17c = the maximum segments per window, +0x60 = mccore tol, +0x68 =
  converter +0x180 = the filter tolerance (arc-fitter branch: 1e-6 when dat[0x4d] == 4, else dat+0x10 x 0.9);
  0x4008fe3cc6ff87db = 179 degrees, the largest sweep of one output arc piece.
- Per curve the polyline (each record's start + the last end; every input record must be a line, else -2) goes
  through FUN_18020e0b0: a window grows one segment at a time; line deviation FUN_18020f2c0 (distance to the chord,
  with a penalty beyond its ends); FitArc3D FUN_18020ea80 (planarity, centre from perpendicular bisectors
  FUN_180211a70 with radial spread <= 10 tol, recursive 3 x 3 grid search FUN_18020e260 over points and chord
  midpoints, cr_a_3p / constr_ns_arc; radius >= 50 tol, |sweep| <= 5.969026, centre within 10000); commit / write
  FUN_18021eec0 / FUN_18020f6e0 (arc when line deviation >= tol or > arc deviation), writers FUN_18021f470 /
  FUN_18021f080, FUN_18021e8e0, flush FUN_18020f630. Output records: start +0, end +0x18, arc centre +0x30, unit
  normal +0x48, type +0x60 (1 / 2), +0x61 = 1 on the curve's first record; arcs split into (int)(|sweep| / 179
  deg) + 1 pieces (rotate_pt) sharing piece 0's normal; bytes +0x62..0x67 (and +0x30..0x5f of lines) are stale heap
  bytes. **Ported**: viewer/ifk/ifk_hbefilter.c `hbe_filter` (XY plane; regression in tools/tests/ifk_silhouette_test.sh,
  not checked against the DLL).
- Arc-fitter branch (Turn Profile with dat[0x4f] and !dat[0x53]): the filter runs with lines only, FUN_18020c830 ->
  tp_ents, **CSpinBdryArcFitter** (mcmachinedef.dll 1800e06f0; constructor / EvalIFBdry / ToIFBdry / ProfileView
  mapped, internals DefineNamedView / FitArcsToIFLines / FindOuterBdry **NOT DECODED**), FUN_18020cb90 back (dat[0x4c]
  = 1 or 2 passes, one mirrored in y; the axis points in dat+0x18 / +0x30 are rewritten in profile-view coordinates).
- FUN_18021ff80 (Silhouette only): a pock_bound of the curve's unfiltered records (2D, FUN_18020c6d0 / c5a0 / da60);
  MCCECalcOffset(1, -|tol x 10|, .., 0x100, 0, 1e-6, 1e-6); no result -> the curve is dropped (neither filtered nor
  written). [inferred] removes boundaries too thin for an offset of 10 tol; the offset side NOT DECODED.
- FUN_180232540(FILE, flat, 0, 1.0, z, CHAIN** out, maxang = dat+8, undo): flat = (spin == 0 || !dat[0x55]) sets all
  z to the depth; lines are collected into a point list (duplicates within tol dropped), broken at an angle > maxang,
  at +0x61, at arcs and at the spline point limit; a flush densifies by calc_chord_ht against curve_chord_dev (depth
  20) and calls mucbMakeMcSpline (2 points = line, 3+ = spline with unit end-chord tangents); arcs by mucbMakeMcArc3D
  (constr_arc_new in x, y, clockwise when normal.z < 0, active view, depth centre.z); mucbAddCurveToChain (nc_tol)
  appends / prepends, reversed if needed; closed chains go to the output, and an open leftover is dropped when any
  chain closed. Not ported (needs mccore calc_chord_ht / make_spline_from_pts / add_curve_to_chain, NOT DECODED).
  **2026-11-12: all of 10.5 decoded, ported and checked against the DLLs - section 14** (offset check: MCCECalcOffset's result open).

## 11. Silhouette Boundary kernel: interfacial11 Silhouette::Run (18001f1c0) - decoded 2026-11-11 (O3 part 3)

Dumps: decomp/interfacial/i11_silhouette_all.c (Run at l.24006), ifsilhouette_all.c (IFSilhouette::Run 1800264f0 l.36189),
new i03_planecut_all.c (interfacial03, every function matching PlaneCut + callees, 561 functions, 45k lines).

### 11.1 Parameter blob (packed by IFSilhouette::Run, read back by Run after un-mapping it through the byte table at
this+8 +0x100) [code]
| bytes | IFSilhouette sends | Run |
|---|---|---|
| string | tpiftools this+0xcc | only the temp file name of the output post-processing (11.4) |
| string | input path | Surfer::ReadFile (binary IFF surface; Surfer::ReadSTL only when SilhouetteData+0x71, set by SetSTLInputData only) |
| string | output path | Surfer::WriteCurves |
| byte | 1 | write the output at all |
| byte | 1 | WriteCurves' binary argument |
| double | tol (tpiftools tol x scale) | Stitch2D tolerance (FUN_180012340) and tol^2 in FUN_180025000 |
| byte | 1 | SilhouetteData+0x70: angle filter mode, 1 = keep min <= angle <= max, 0 = keep angle < min or > max |
| double | 0.0 | SilhouetteData+0x60 = minimum angle (degrees x pi / 180) |
| double | 90.0 | SilhouetteData+0x68 = maximum angle |
| byte | 0 | route: 0 = Surfer -> SurferToSilhouette; else Surfer -> Stitch -> Trip (not used by Mastercam) |
| string | param_5 | the licence check FUN_180001000 and the Stitch constructor (out of scope) |
The angle is acos(nz / |n|) of the facet normal turned upwards, so with 0 / 90 / mode 1 every non-vertical facet is
kept. |tol - 0.1234567| <= 1e-6 aborts with the message "May 10 1954 and July 27 1976." (l.24170).

### 11.2 Stages [code unless marked]
1. Grid FUN_18001e350(2^-20) (disassembly 18001f500), the same grid as Turn Profile; snapping down: floor(v / g) x g.
2. Input through SurferToSilhouette (vftable 18021b9e8): AddNode FUN_180023710 (x, y, z snapped, deduplicated in a
   tree ordered lexicographically on (x, y, z), exact integer copies kept); AddTriangle FUN_180023920: n = (p2 - p1) x
   (p0 - p1) in the filtered arithmetic (FUN_18002bad0), z exact (FUN_180020b10) when uncertain; nz == 0 -> dropped
   (vertical); nz < 0 -> p0 / p2 swapped and n negated; angle filter; kept facets prepended to SilhouetteData+0xa0,
   xy box, their 3 edges (node-pointer pairs) in an edge tree with the facet listed on the edge. End FUN_180023f80:
   overflow -> "IFSilhouette error 2: overflow."; one PlaneCut cut-plane set: normal (0, 0, 1) at 0.0; each
   **silhouette edge** (one kept facet, or two kept facets running it in the same direction = a fold seen from above),
   leftmost in the tree first, becomes a vertical triangle for PlaneCut: (start x, y, 0), (end x, y, 0) and the
   snapped midpoint at z = floor(1 / g) x g = 1.0 (start / end in the first facet's order).
3. PlaneCut::Run(label, 1, 0.0) (interfacial03) with PlaneCutToSilhouette (vftable 18021beb8): the walls cut by the
   plane z = 0 give the projected silhouette edges; PlaneCut builds their planar arrangement and outputs its face
   boundaries as a curve tree (slots 1-6: tree nodes, points, edges) and asks the IO to classify one test point per
   face (slots 7-10: FUN_180022c00 / cb0 / da0 / e80): a point strictly inside a kept facet (FUN_180020c10, filtered
   box reject + three orientations) passes (PlaneCutBase::TestPointPassed); a point on a facet edge / vertex is
   decided with a small Shell2D of the facets touching it (FUN_1800043f0 / 4840 / 49a0 / 4a50, FUN_180021830).
   Corrected from the PlaneCut decode (11.6): the test points belong to the **edge chains**, not to faces, and a
   point "passes" when it is **not strictly inside** any kept facet's projection (on the boundary of 0 or 1 facets it
   passes, on 2 or more the Shell2D test decides); chains whose point passed are kept, so the output is the boundary
   of the union of the projected kept facets (outer loops and holes).
4. Clean-up: per tree node a Stitch2D (FUN_180011fb0 / FUN_180012340(tol) / FUN_180012e00; "Stitch2D error 1: memory
   leak.") rebuilds the loops into a second tree; FUN_180025000(tol^2) removes a node with exactly one childless child
   when each loop lies within tol of the other (FUN_180024600: a zero-width outer / hole pair).
5. Output FUN_180024da0 through Surfer: NewWriteCurves, AddCurveTreeNode / Branch, AddWriteCurveNode(x, y, 0),
   AddWriteCurveEdge; Surfer::WriteCurves(out, binary, false, byte table, buffer) (CompressCurves skipped). mcmsurf
   reads the file with format 1 (the two extra int32 per curve = tree data [inferred]). Surfer::WriteCurves
   (interfacial01 18000f480 -> FUN_180017b20, dump decomp/interfacial/i01_surfer_curves.c) [code]: binary header
   `binary tree curves` (18 bytes, **a space, not CurveXferMC's underscore**) when the curves are a tree (+0x7c),
   else `binary curves` (13); "tagged binary ..." variants (20 / 25 bytes) only with its last argument, which
   WriteCurves passes as 0; then int32 node count (+0x18), int32 edge count (+0x1c), the nodes (24 bytes each), then
   the edges per curve through FUN_180019880 (decoded in 11.9.4); every byte through FUN_180016350 (the byte table).
   Text mode writes "curves" / "tree curves", the counts, "nodes", "segments" lines. Both headers pass mcmsurf's
   check (first byte 'b') and GetDataFileFormat ('t': byte 7 't', byte 12 'c').

### 11.3 Shell2D / Stitch reuse [code]
Shell2D is not on Silhouette's main path (only the test-point side path of step 3). The interfacial11 copy is a
different (optimised, TLS) build of the class: of 269 functions reachable from the ported ifspinsil Shell2D entries
only 9 trivial ones match text-wise; counterparts by error string: i11 FUN_180002c90 / ifspinsil FUN_18003adb0 (error
2), FUN_180003250 / FUN_18003a440 (10), FUN_1800034c0 / FUN_18003a300 (9), FUN_180003a90 / FUN_18003a1d0 (5),
FUN_180003b10 / FUN_18003be10 (8), FUN_180005b10 / FUN_18003d4c0 (6, seed), FUN_180004a50 / FUN_18003e860 (walk).
Differences found: the i11 seed first merges a filtered-equal vertex when the extreme vertex has no edges; the i11
vertex insert keeps no extreme vertex. So the ported Shell2D is **not** reusable as is (equivalence not established).
Stitch (interfacial02) is only on the unused route; Stitch2D has no counterpart in the port.

### 11.4 Output post-processing (ifsilhouette FUN_180026a40) [code]
Surfer writes the file through the byte table; FUN_180026a40 inverts the table, copies the file into a temp file named
from the blob's first string mapping every byte back, deletes the output and renames the temp file over it ("Failed to
postprocess output." on failure). The file mcmsurf reads is the plain one.

### 11.5 NOT DECODED (state 2026-11-12, after stage C)
The whole of Run on Mastercam's path is decoded and ported (11.6-11.9: input, PlaneCut for one z plane / mode 1, the
classifier, Stitch2D, FUN_180025000, the Surfer writer; `ifk_silhouette_run`). Left:
- Surfer::ReadFile / ReadSTL: the order in which they call AddNode / AddTriangle (array order assumed).
- Index structures replaced by candidate scans with an equivalence argument [inferred]: PlaneCut's quadtrees
  (Polygon2DQuadtree > 100 edges, the PlaneCutToSilhouette test-point quadtree, the Shell2D EdgeQuadtree; 11.8.6) and
  Stitch2D's octree (11.9.2). Their candidate ORDER is not decoded; it matters only for Stitch2D split points with
  equal keys (counted as ties).
- Details listed as [inferred] in 11.8.6 (slot-4 tree-node argument, FUN_18003e0f0 / FUN_18003dba0 dropped
  arguments, FUN_1800160c0).
- The dialog's reaction to messages with argument 1 ("IFSilhouette error 2", "Surfer error 62", "Shell2D error 6"
  continues with a NULL edge in i11, 11.8.6).
- Not ported (not on Mastercam's path): route 1 (Stitch -> Trip), CompressCurves, the memory output, text formats,
  PlaneCut modes 0 / 2 and several cut planes. Out of scope: the byte table, licence checks.
- Oracle comparison against Mastercam: done 2026-11-12, section 13 (all cases byte-identical).

### 11.6 Ported: the input stage; PlaneCut decoded in outline (2026-11-11; detail in
decomp/interfacial/notes_i03_planecut.md and notes_i11_silhouette.md, not in git)
- viewer/ifk/ifk_silhouette_in.c `ifk_silhouette_walls`: grid, AddNode (snap + exact dedup; a hash instead of the
  tree, lookup only), AddTriangle (filtered nz FUN_18002bad0, exact fallback FUN_180020b10 with FUN_18002ca90 /
  FUN_18002bc50 / FUN_180002ab0 / FUN_180010340 / FUN_180010900, back to double by FUN_180026d10 with scale g x g:
  disassembly 180023a86..180023ab5, the decompile lost it), the angle filter, the kept list, the edge tree and the
  end slot (silhouette edges, wall triangles: AddInputNode order mid, start, end; ids from 1). Orders the DLL takes from
  node pointers use the emulated pool addresses (13.3 item 3; was creation order). Surfer::ReadFile's AddNode / AddTriangle call order is
  NOT DECODED (input array order assumed). Self-test tools/tests/ifk_silhouette_test.sh: box -> the 8 top / bottom
  square edges, 24-facet cylinder -> both 24-gons, 32 x 16 UV sphere -> the 32 equator edges: ALL PASS.
- PlaneCut (interfacial03) [code, outline]: the cut-plane set (FUN_180025350 / FUN_1800255e0: normal and offsets
  snapped, planes in a tree; ground = -DBL_MAX, Silhouette never sets it); EndInput(true) builds shared triangle
  edges; Run FUN_180025910: triangles to planes within +-1e-7, FUN_180026140 cuts each (exact vertex signs; the walls
  cut to exactly their base edge), quadtree of cut edges (FUN_180026b60 / FUN_18002fee0), pair intersections
  (FUN_180027060 / FUN_180027500, filtered orientation with exact fallback FUN_18001bde0, pairs from the same triangle
  id skipped), splits FUN_180028c20, FUN_180029630 (below-ground edges dropped, dangling edges pruned FUN_180029e40,
  angle sort FUN_180029f90, chains through degree-2 nodes), one test point per chain (an edge midpoint,
  FUN_180014710), IO slots 7-10, mode 1 keeps the passed chains (FUN_1800151f0), FUN_1800152c0 assembles them into
  Polygon2D loops nested by FUN_180015d20 (error 47 "multiple parents") and outputs slots 0-5.
- NOT DECODED: the exact cut-node / intersection arithmetic (FUN_18001b800, FUN_18001bde0 + about 25 helpers),
  FUN_18001d8d0 (edge piece flags), the angle-sort rule of FUN_180029f90, Polygon2D (FUN_18003cec0 / FUN_18003d480),
  FUN_180021830 / FUN_180020c10 (classifier details), Stitch2D, the edge part of Surfer's writer (FUN_180019880).
  Estimate for the rest of a Silhouette port: arrangement with exact predicates (~2,500 decompiled lines), Polygon2D
  and nesting (~1,000), test points + classifier (~400, plus the i11 Shell2D), Stitch2D, FUN_180025000, the writer:
  several sessions; FUN_18001bde0 first. `ifk_silhouette_run` does not exist yet.


### 11.7 PlaneCut port, stage A (2026-11-11): up to and including the split arrangement
`viewer/ifk/ifk_planecut.c` / `.h` (every step cites its function in a comment), self-test
`tools/tests/ifk_planecut_test.sh` (make ifk_planecut_test). Decompiles: decomp/interfacial/i03_planecut_all.c, new
i03_qtree.c (quadtree slots, InputTriangle slot 0), i03_dis_27500.txt (disassembly of FUN_180027500 and
FUN_180026b60), i03_vt_tri.txt (vtables). Hand-over for stage B: decomp/interfacial/notes_planecut_stageA.md.
Scope: axis-aligned cut sets with a non-zero z normal (Silhouette's (0,0,1)); other sets return -1 (not ported).

#### 11.7.1 The exact layer [code]
- **SuperInteger<n>** (fixed n, templated): sign byte (1 = positive) + n 32-bit limbs, least significant first.
  Input coordinates are SuperInteger<1> = trunc(v / g) (FUN_180008dc0: v <= 0 -> sign 0 and q negated; |q| >= 2^32 sets
  the overflow flag DAT_18008f680). Add / subtract FUN_18001b700 (n 1), FUN_180017c90 (2), FUN_180018190 /
  FUN_180018320 (3), FUN_18001fe90 (4), FUN_1800200f0 (5): equal signs -> add, carry out of the top limb -> overflow;
  else top-down magnitude compare, equal -> zero with sign 1, else larger minus smaller (FUN_180018490) with the
  larger's sign. Products FUN_180001180 (limb vector x scalar) and its users FUN_180016dc0 (1x1), FUN_18001b5c0
  (2x1), FUN_180020000 (2x2), FUN_18001f600 (3x2), FUN_18001f510 (1x4): exact, sign = (equal sign bytes).
- **PlaneCutSuperDouble** (0x68 bytes): sign byte, limbs, pointer to the lowest limb (+0x58), exponent E in words
  (+0x60), count n (+0x62); value = sum limb[i] 2^(32 (E - n + i)). From SuperInteger<nl> (FUN_18001eda0 nl 1,
  FUN_18001eec0 / FUN_18001fb10 2, FUN_18001f230 4, FUN_18001f730 5): shift left by (nl k) mod 32, E = (nl k) / 32 + 1,
  n = nl + 1, zero limbs trimmed at both ends; k = DAT_18008f668 = 1 + the number of doublings of (uint)(1 / g) below
  2^31 (FUN_180041800) = 12 for g = 2^-20, so the value is m g^nl exactly. Products FUN_18000e0b0 / FUN_180017170 /
  FUN_180017480, differences FUN_180017340 / FUN_1800203d0 / FUN_18001fcf0 / FUN_180016fd0 (a bden - b aden), sum
  FUN_18001fc50, compare FUN_18000e260; temporaries of 0xfb8 bytes on a TLS stack (FUN_1800416e0, DAT_18008f608).
  To double: **FUN_1800161f0** = sequential sum from the lowest limb of (double)limb x 2^(32 (E - n + i)) (scale by
  pow(2, ...), then x 2^32 per limb), negated for sign 0. Limbs sit on absolute 32-bit boundaries, so the limb
  decomposition of a value is unique and any exact implementation converts identically; the port keeps its own
  SuperDouble (e = E - n) and only the conversions are bit-exact.
- **Exact cut-node record** (pool impl+0x1a0, attached to the CutNodeCoordinate +0x50): homogeneous (U, V, D [, W])
  as SuperDoubles at +0 / +0x68 / +0xd0 / +0x138, +0x1a0 = W present. **FUN_18001bde0(node, plane, wantW)** builds it
  for the head node of the point's list (lazily: returns when a record exists, unless W is wanted and missing):
  kind 0 (input node): (u, v, 1 [, w]) from the node's SuperIntegers, the doubles stay. Kind 1 (input edge a-b,
  aligned set): delta = b - a (SuperInteger<1>, error 2 when delta[w] = 0), U = a.u delta[w] + (c - a.w) delta[u],
  V likewise (SuperInteger<2>), D = delta[w], W = c delta[w] (c = the plane's SuperInteger offset, plane +8).
  Kind 2 (intersection of the planes of triangles A and B with the cut plane): FUN_18001b150 per triangle
  n = (p2 - p1) x (p0 - p1) (SuperInteger<2>, FUN_18001b390), d = n . p0 (SuperInteger<3>); FUN_18001f380
  cr = nA x nB (SuperInteger<4>); D = cr[w] (0 -> error 5); U = c cr[u] + dA nB[v] - dB nA[v],
  V = c cr[v] - dA nB[u] + dB nA[u], W = c cr[w] (SuperInteger<5>). Then (LAB_18001c41a) all four are negated when
  D < 0, and for kinds 1 / 2 the point's doubles are **replaced**: x = U / D with
  e = (|D| |x| + |U|) / (|D| - |D| 2^-52) + |x| (same for v, and w when wanted). So a point's double coordinates can
  change when an exact predicate first needs it [code]. Non-aligned branches (FUN_18001f000, FUN_180016e20,
  FUN_18001f140, FUN_18001f870, FUN_180020260, FUN_18001f9c0): not ported.
- **Comparison with the Shell2D exact layer of ifk_spinsil.c** (task: reuse only provably identical code):
  | interfacial03 | ifspinsil (ifk_spinsil.c) | verdict |
  |---|---|---|
  | FUN_18001b700 / 180017c90 / 180018190 / 18001fe90 / 1800200f0 (add, n = 1..5) | FUN_18000ed50 `big_addsub` | same computation (same sign convention, carry -> overflow, top-down compare, zero -> sign 1); fixed vs run-time n |
  | FUN_180018490 (magnitude subtract) | FUN_18000f090 `mag_sub` | same |
  | FUN_180001180 + users (products) | FUN_18000eac0 `big_mul` (FUN_1800045b0) | same result (exact product, sign = equal bytes) |
  | FUN_18001f4c0 / inline loops (sign) | FUN_18000e8b0 `big_sign` | same |
  | FUN_180008dc0 (double -> SuperInteger<1>, trunc, g = 2^-20) | FUN_180004f20 (double -> 2 limbs, floor split, g = 2^-40) | different; identical instead to interfacial11 FUN_180026d90 (`si1_from_double`, ifk_silhouette_in.c) |
  | FUN_1800161f0 (SuperDouble -> double, word exponent) | FUN_180004cc0 (fixed scale, all limbs) | different representation, not reused |
  | SuperDouble products / sums / compare | none | new |
  | FUN_180001040 (filtered product) | FUN_180004030 | same formula e = eb |a| + ea |b| + ea eb 2^-53 + |v| |
  | FUN_180028090 (filtered orientation, fixed term order) | FUN_180039eb0 (line record) | different formula / order, not reused |
  The identical functions are static in ifk_spinsil.c; ifk_planecut.c carries its own copy of the same algorithm
  (cited to both addresses) rather than changing the Turn Profile file.

#### 11.7.2 Cut-plane set, input, preprocessing [code]
- FUN_180025350 / FUN_1800255e0 / 180021020 as in 11.6; the plane tree has the smaller offset on the left (+0x90) and
  the larger on the right (+0x98) (FUN_1800252b0 / FUN_1800255e0). **Run starts at the rightmost plane and steps to
  the in-order predecessor: planes are cut in decreasing offset order** (correction to 11.6 / the notes).
- AddInputTriangle pushes on impl+0x88 (head = newest). Preprocessing walks impl+0x88 from its head and pushes each
  triangle on every plane with lo <= offset <= hi (FUN_1800252b0 first >= lo, FUN_180025210 last <= hi), so a plane's
  list, popped from its head by the cut loop, is in AddInputTriangle order; cut edges are pushed on impl+0x90, so
  the pair and split loops see them newest triangle first. EndInput(true) walks impl+0x88 from its head and keys
  each edge by its two InputNode pointers, smaller first (the end FUN_18001b800 interpolates from).
- Heights h = (ny y + nx x + nz z) / |n|^2, min / max by the decompile's comparison chain, lo - 1e-7, hi + 1e-7,
  lo raised to the ground for an aligned z set (ground = -DBL_MAX unless SetGround).

#### 11.7.3 Cut FUN_180026140, nodes, points [code]
- Vertex sides (aligned): z == offset -> 0, offset < z -> +1, else -1 (double compares). Per triangle edge i (i, i+1):
  side(i) = 0 and side(i+1) != 0 -> vertex i; side(i) != 0, side(i+1) = 0 -> vertex i+1; opposite non-zero sides ->
  edge i; both 0 or same side -> nothing. FUN_1800267e0 drops repeats (pointer compare). Exactly 2 records -> a cut
  edge; they are swapped when they are (vertex 1, vertex 0). Coordinates FUN_18001b800: vertex -> (u, 0, v, 0, w, 0);
  edge (aligned) t = (offset - a.w) / delta.w, w = offset (error 0), u = t delta.u + a.u with
  e = |u| + |delta.u| |t| + |t delta.u| (error 20 when delta.w = 0). Box = value -/+ e 2^-52; discarded when its z
  max is below the ground or IO slot 6 says so (PlaneCutToSilhouette FUN_180020b00 returns 0).
- Direction (bVar23): equal record indices i -> side(i+2) < 0; first index 0: second 1 -> side(2) < 0, second 2 ->
  side(1) > 0 when the first record is vertex 0, else side(0) < 0; first index != 0 -> side(0) < 0; true swaps start
  and end. Flag +0x48 = 0 only when both nodes are vertices and no vertex is above the plane (below, flipped).
  For a Silhouette wall (start, end, mid z = 1): the base start -> end, flag 1.
- FUN_180026a30: cut nodes are unique per input pointer (tree impl+0xc0, cleared per plane); a repeat frees its
  record and point. A new node is pushed on its point's list (+0x40) and **FUN_180028aa0** merges points: key
  s = v + u, es = ev + eu + |s|; FUN_180030470 compares (s certainly different -> by s; both es = 0 -> u then v as
  doubles; else FUN_1800286f0 same definition (kind 0 same xyz, kind 1 same end coordinates) or exact (U + V) / D,
  then U / D, V / D); equal -> the node list moves onto the existing point (head first), its exact record too when the
  existing has none, the new point is freed. New points are pushed on impl+0xa0.

#### 11.7.4 Pair intersections FUN_180027060 / FUN_180027500 [code]
- FUN_180027060: per cut edge E (impl+0x90 from its head): stamps DAT_18008f518 / DAT_18008f470 += 2, E's two points
  and the points of its current split list are marked; then the edges sharing a quadtree leaf with E that were not
  processed yet (each processed edge leaves all its leaves) are tested once each (+0x4c stamp).
- FUN_180027500(A = E, B): same triangle id -> nothing. B's end points marked (= on A already): both -> nothing; one
  (n) -> FUN_180028580: when n is A's own end node, the other end of B and the far end of A on opposite sides of n
  in u or in v (both certainly, FUN_1800285f0) -> nothing; no mark -> filtered box test. Stage 1: o0 / o1 =
  FUN_180028090(B0 / B1, A0, A1 - A0) = (P.u - A0.u) d.v - d.u (P.v - A0.v) with
  e = ed.u |pv| + epv |d.u| + ed.u epv 2^-53 + |d.u pv| + ed.v |pu| + epu |d.v| + ed.v epu 2^-53 + |d.v pu| + |o|;
  "certain" = |o| > e 2^-52. Decision table (exact = needs the exact stage, cand = existing node at the meeting point):
  o0 certain: o1 certain -> same side (o0 >= 0 and o1 > 0, or both < 0) nothing, else go on; o1 uncertain -> B1
  marked: nothing; e1 != 0: exact; e1 = 0: cand B1. o0 uncertain: B0 marked -> o1 certain: nothing; B1 marked:
  nothing; e1 = 0: collinear; else exact. e0 = 0 -> o1 certain: cand B0; B1 marked or e1 = 0: collinear; else exact
  with cand B0. Otherwise exact. Stage 2 the same with A's ends against B (q0, q1): q0 certain: q1 certain -> same
  side nothing; q1 uncertain -> e != 0 exact, else cand A1. eq0 = 0 -> cand A0; q1 uncertain -> eq1 = 0 collinear,
  else exact. Otherwise exact. Exact stage (FUN_18001bde0 on the four ends, FUN_180016fd0 directions,
  FUN_180028230 = sign of (P.U A0.D - A0.U P.D) dy - (P.V A0.D - A0.V P.D) dx): the same table on exact signs, cand
  reset first; a second candidate -> "PlaneCut error 41 / 42 / 43: undetected equivalent nodes."; a candidate whose
  point is marked -> nothing.
- No candidate -> new kind-2 node (triangles A, B). Filtered (disassembly 180027d26..): x1 = dA.u dB.v,
  x2 = dA.v dB.u (FUN_180001040), den = x2 - x1, eden = e1 + e2 + |den|; certain -> t = q0 / den,
  et = (eden |t| + eq0) / (|den| - eden 2^-52) + |t|, u = dA.u t + A0.u,
  e = edA.u |t| + et |dA.u| + edA.u et 2^-53 + |dA.u t| + eA0.u + |u| (v alike), w = offset; uncertain ->
  FUN_18001bde0(.., 1). Then FUN_180028aa0. The node (new or candidate) is pushed on both edges' split lists.
- Collinear FUN_180028830: FUN_180029390 picks the axis (u when the ends differ certainly in u, else v, else exact;
  equal ends -> error 4); FUN_180013330 compares along it (filtered; both errors 0 -> equal; else exact); an end of
  one edge strictly between the ends of the other goes on the other's split list.
- **Quadtree (not ported, [inferred] equivalence):** FUN_180026b60 builds an IntersectionOctree (Quadtree template,
  the same traversal FUN_18002fee0 as Shell2D's): root = box of the cut edges, tolerance (+0x30) =
  max(1, node errors) x 2.22e-15 (correction to 11.6: not max |coord|), split while side > the smallest cell and
  > 5 entries (slot 2 FUN_18001d700), FUN_18003c190 distributes with the classifier FUN_18001e6e0 (an edge within
  the tolerance of the split line goes to both halves; edges are clipped at the line, FUN_18001ece0), slot 3
  FUN_1800301e0 re-flags crowded children, leaves with >= 2 entries become groups appended to each edge's +0x38 list
  (slot 0 FUN_18001d5c0). It only proposes pairs. The port tests, for E in list order, every later edge whose closed
  box meets E's box, in list order. Pairs that produce a node share a point, hence a leaf; disjoint boxes are
  rejected by FUN_180027500 anyway. **The test order is ours**; it decides which pair creates a crossing node first,
  hence the emulated address of its point and the head of a point's node list (FUN_18001d8d0 takes the first
  matching kind-2 node, which matters only where three or more walls cross at one point).

#### 11.7.5 Split FUN_180028c20 and piece flags [code]
- Per cut edge (impl+0x90 from its head): empty split list -> one graph edge. Else the end points are marked, the
  split list's points not marked yet are collected (pushed on a list, so read back in split-list order); none -> one
  edge; one point X -> pieces (A0, X), (X, A1), and r = FUN_18001d8d0 at X with the direction A1 - A0: r > 0 flags
  the first piece (+0x10), r < 0 the second. More -> FUN_180029390 (error 7 if the ends are equal), the points and
  then A0, A1 go into a tree ordered by FUN_180013330 (equal -> error 3, goes right), walked from the smallest; the
  direction for FUN_18001d780 is A1 - A0 when the smallest is A1, A0 - A1 when it is A0; each piece keeps A0 -> A1
  orientation; at each point after the first r = FUN_18001d8d0: r < 0 flags the piece ending there, r > 0 the next.
- FUN_18001d8d0(direction d, point X): the first kind-2 node in X's list made with this edge's triangle names the other
  triangle T; none, T's flag (+0x3c) set, or no vertex of T strictly above the plane (below when flipped) -> 0. Else
  the sign of N_u d.u + N_v d.v with N = (T2 - T1) x (T0 - T1) (filtered: N_u = a_v b_w - a_w b_v with
  e = |a_w b_v| + |a_v b_w| + |N_u|, products FUN_180001040, sum certain -> its sign; else exact, FUN_18001e4d0 /
  SuperInteger normal / FUN_180017480 / FUN_18001fc50; 0 -> "PlaneCut error 8"). For walls N is horizontal,
  perpendicular to T's base. **FUN_180029630 removes every graph edge with +0x10 set** (and those with +0x15 =
  +0x16 = 0), so the flag marks the piece of a cut edge on the material side of the wall it crosses [inferred
  meaning: for Silhouette, the part of a silhouette edge hidden inside the neighbouring facet's projection].
- FUN_180029150 (graph edge, pool impl+0x158): ends = the two points, **[0] = the smaller point address**; one record
  per unordered pair (tree on the pair); new: +0x11 = triangle flag, +0x13 / +0x14 = seen from [0] / from [1], +0x15 =
  the cut edge's +0x48, +0x16 = 0 (pool default 1; cleared because InputTriangle vtable slot 0 = FUN_1800012c0
  returns 0), [3] = triangle data unless flagged; pushed on impl+0xa8. Repeat: +0x11 |= flag, (+0x16 set: clear
  +0x13..+0x16), +0x15 given and not set -> +0x13 = +0x14 = 0, +0x15 = 1; then the direction byte; [3] = max.

#### 11.7.6 Orders taken from pointers [code for the pool, assumption between blocks]
The interfacial03 MemoryPool fills its free list from the first to the last record of a new block of 1000
(FUN_18000d200, FUN_1800267e0 inline, FUN_1800313f0, FUN_18003bd60) and frees by pushing on the head, so a fresh
block hands out **decreasing** addresses and a freed record is reused first. The port emulates this for InputNode
(EndInput's edge ends -> which end FUN_18001b800 starts from) and CutNodeCoordinate (graph-edge [0] / [1], hence
+0x13 / +0x14): key = block x 1000 + slot, later blocks assumed at higher addresses. This refines the section-8
convention (creation order) with the decoded pool order; the order between blocks (> 1000 records) is not knowable.
Lookups keyed by pointers (node tree, point tree, graph-edge tree, EndInput tree) give the same answers in any
order and are hash maps / a treap in the port.

#### 11.7.7 Self-test (tools/tests/ifk_planecut_test.sh, 2026-11-11)
Square (4 edges), crossing walls with node (1.5, 0.5) exact and the piece flags, crossing at (1, 1/3) within 1 ulp,
T-junction, T-junction on a sloped wall (filtered orientation exactly 0 with error > 0 -> exact path), collinear
overlap (3 edges, the shared piece once), three concurrent walls (one node (1, 1) of degree 6, kind-2 points merged
through the exact path), same id (pair skipped), pentagram (15 pieces, 10 points), and the walls of
ifk_silhouette_walls: box (8 walls -> 4 edges), 24-facet cylinder (48 -> 24), UV sphere (32 -> 32): ALL PASS.
Also checked once (scratch program, not in git): 150 random walls on a coarse grid with many T-junctions and
collinear overlaps, 6 seeds, against an exact __int128 brute force: point and edge sets identical (up to 4274 edges),
coordinates within 4e-11 g. NOT checked against Mastercam (N11-5).

#### 11.7.8 NOT DECODED / not ported
- Not ported: non-aligned and vertical cut sets (FUN_180012180, FUN_18000dfb0, FUN_180016e20, the non-aligned
  branches of FUN_18001b800 / FUN_18001bde0 / FUN_180027500 / FUN_18001d8d0, FUN_18002a610), the buffered variant
  (impl+0xd8, PlaneCutBuffered, IO slot 6 on buffered triangles), FUN_18001d220 (vertical planes), the
  IntersectionOctree itself (11.7.4), IO slot 11 (no-op for Silhouette).
- NOT DECODED: whether a dialog error (slot +0x40, second argument 1) aborts the run (the port records the message
  and continues); where the point tree / point pool are reset between planes (FUN_180029630 or later; the port
  clears per plane and keeps the pool); the heap order of pool blocks. (Answered in 11.8.1: the FUN_180029630 tail.)

### 11.8 PlaneCut port, stage B (2026-11-11): FUN_180029630 onwards, Silhouette's classifier, the curve tree
Code: `viewer/ifk/ifk_planecut.c` (stage B appended; `ifk_pc_run(pc, mode, io, &tree)`, `IfkPcTree`, test-point
accessors `ifk_pc_tp_coords / _exact / _passed`), `viewer/ifk/ifk_sil_classify.c` (PlaneCutToSilhouette slots 7-10),
`viewer/ifk/ifk_sil_shell2d.c` (the interfacial11 Shell2D side path + FUN_180021830, with its own function-by-function
comparison table against ifk_spinsil.c in the file header), self-tests `tools/tests/ifk_planecut_test.sh` (extended)
and `make ifk_s2_test`. New dumps: decomp/interfacial/i03_curvenode.c (CutCurveNode slots, CutTestPoint slot 0
FUN_1800148b0, FUN_180012dc0), i03_stageb_extra.c (FUN_180016cc0, FUN_1800180f0), i03_vt_curve.txt (vtables),
i11_dis_4a50.txt / i11_dis_6b80.txt. Hand-over for stage C: decomp/interfacial/notes_planecut_stageB.md.
Scope: one cut plane, mode 1, axis-aligned non-flipped set (Silhouette's); `ifk_pc_run` returns -1 otherwise.

#### 11.8.1 FUN_180029630 (per plane, after FUN_180028c20) [code]
1. Graph edges from the head of impl+0xa8 (newest first): +0x15 = +0x16 = 0 -> +0x10 = 1; FUN_180012f50 (sign of
   w - ground, filtered; with the ground -DBL_MAX always +1) for [0], and for [1] when [0] < 1: [1] < 0, or [1] = 0
   and [0] < 0 -> +0x10 = 1. Edges with +0x10 = 0 are pushed (front) on both points' +0x48 lists.
2. Points of impl+0xa0 (from the head) with exactly one edge are appended to a queue; FUN_180029e40 prunes: pop a
   point, pop the HEAD edge of its +0x48 list; unless the edge's +0x11 is set: remove it from the other end's list,
   +0x10 = 1, and queue the other end when it is left with exactly one edge.
3. FUN_180029f90 at every point (impl+0xa0 from the head) with >= 3 edges (11.8.2), then FUN_180029e40 again on
   the points it queued. (impl+0xd8 buffered loop with FUN_180003d90: PlaneCutBuffered only.)
4. DAT_18008f470 += 2. Chains: for each graph edge (impl+0xa8 from the head) with +0x12, +0x10, +0x11, +0x16 all 0:
   a CutCurve (pool +0x180, [3] = plane), +0x12 = 1, a FIFO of edges. Per popped edge: FUN_1800131d0 on both
   points (CutCurveNode, once per point at +0x60: +8.. = (u, v, w) doubles at that moment, +0x30 = (FUN_180012f50 == 0),
   mode 1: +0x38 = {FUN_18002a550 copy of the point (six doubles + a copy of its head cut node), plane}); at [0] then
   [1]: when that point has exactly 2 edges, the other one (not +0x12 / +0x11 / +0x16) gets +0x12 = 1 and is
   appended. A CutCurveEdge (pool +0x178) is pushed front on the curve's +0x10 list: +0x13 = 0 -> ([1] node, [0]
   node), byte +0x10 = 0 and the edge becomes "last"; else ([0], [1]), +0x10 = +0x14, "last" when +0x14 = 0; both
   nodes' +0x20 counts +1; [3] = edge data. So a curve = a connected run through degree-2 points (or a cycle).
   With a "last" edge: the curve is appended to impl+0xb0 (FUN_180009de0) and its [0] / [1] = that edge's two
   points (a point already stamped by another curve -> a FUN_18002a550 copy, else stamped and used). Without one
   (every edge both ways): the curve is discarded (edges freed, nodes whose count drops to 0 freed, FUN_18002a480).
5. Tail: all graph edges freed (impl+0xa8 emptied), the point tree impl+0xc8 = 0, and every point of impl+0xa0 not
   stamped in step 4 is freed with its cut nodes and exact record: **this is where the per-plane point data is
   released** (the open question of 11.7.8). The stamped points survive as the curves' ends.

#### 11.8.2 The angle sort FUN_180029f90 [code] - ordering rule decoded
- One CutEdgeAngleSort record (pool impl+0x190) per edge of the point P (its +0x48 list from the head),
  FUN_180013580: O = the other end, dx = O.u - P.u, dy = O.v - P.v with e = eO + eP + |d|; both certainly non-zero ->
  X (+0x20) = dx < 0, Y (+0x21) = dy < 0, key = dx^2 / (dx^2 + dy^2) with e(dx^2) = 2 edx |dx| + edx^2 2^-53 + |dx^2|,
  e(sum) = 2 edy |dy| + edy^2 2^-53 + |dy^2| + e(dx^2) + |sum|, certain sum -> err = (e(sum) |key| + e(dx^2)) /
  (|sum| - e(sum) 2^-52) + |key|; otherwise FUN_180013880 (exact: FUN_18001bde0 on both ends, which may replace
  their doubles, dx / dy = O.U P.D - P.U O.D etc. (FUN_180016fd0), X / Y from their signs, num = dx^2, den = num +
  dy^2 exactly, key = num / den in doubles, err = (|den| |key| + |num|) / (|den| - |den| 2^-52) + |key|).
- Comparison FUN_180013c40(a, b) = a.key > b.key: filtered when |d| > (ea + eb + |d|) 2^-52, else exact
  num_a den_b vs num_b den_a ("PlaneCut error 13" on equality).
- AVL tree (FUN_1800323b0 descent, FUN_18003b3a0 rebalance, rotations FUN_18003c3a0 / FUN_18003c2e0; ported as is).
  Descent rule: new upper (Y = 0) vs upper: both X = 0 -> right iff new.key > node.key; new X = 0, node X = 1 ->
  right; new X = 1, node X = 0 -> left; both X = 1 -> left iff new.key > node.key. Upper vs lower -> left; lower vs
  upper -> right. Lower vs lower: both X = 0 -> left iff new.key > node.key; new X = 0, node X = 1 -> left; new X =
  1, node X = 0 -> right; both X = 1 -> right iff new.key > node.key. **In-order = directions with dy >= 0 by angle
  decreasing from pi to 0, then dy < 0 by angle decreasing from 2 pi to pi: a clockwise cyclic order starting just
  after the -x direction** (cos^2 decides inside a quadrant; dx = 0 / dy = 0 fall on the quadrant borders consistently).
- Walk from the leftmost: skip records whose edge has +0x13 and +0x14 both set or +0x15 = 0; for such an edge E
  ("start" = [0] when +0x13 is set, else [1]): F = the cyclic predecessor when E starts at P (outgoing), the cyclic
  successor when E ends at P (incoming); F with both directions or +0x11 -> next E; F of the opposite kind (one in,
  one out) -> next E; else (both outgoing or both incoming) F is removed from both ends' lists, +0x10 = 1, and its
  other end queued for FUN_180029e40 when left with one edge. F stays in the tree (a later visit may meet it again;
  the removal is then a no-op). [inferred meaning: at a junction, consecutive boundary edges must alternate in / out]

#### 11.8.3 Test points and IO slots 7-10 [code]
- Run "Cleaning up...": slot 7; per curve of impl+0xb0 in order a CutTestPoint (FUN_180031680 / FUN_180014710):
  coordinate [axis index] = (A + B) / 2 of the curve's two points with e = (eA + eB + |A + B|) / 2 (x, y, z for a z
  cut), slot 8 with it; slot 9; slot 10. CutTestPoint vtable slot 0 **FUN_1800148b0** (first call): FUN_18001bde0(.., 1)
  on both points, exact X_i = A_i B.D + B_i A.D (FUN_180017890 = a bden + b aden), W = 2 A.D B.D (FUN_180017a30 with
  the constant 2), and the test point's doubles are **replaced** by X_i / W (FUN_180017bd0 -> FUN_180016cc0,
  e = (|W| |x| + |X|) / (|W| - |W| 2^-52) + |x|).
- PlaneCutToSilhouette (interfacial11): FUN_180022c00 begin (box), FUN_180022cb0 add (record {point, facet list,
  stamp} pushed front on +0xa8, box, quadtree entry), FUN_180022da0 quadtree build, FUN_180022e80(.., 0): per kept
  facet (+0xa0 from the head) the facet box (min / max of the vertices), per candidate point not yet covered and not
  stamped for this facet: FUN_180020c10 = certain box reject -> 0; o_i = FUN_180020d90 on edges (v0 v1), (v1 v2),
  (v2 v0) = (P.x - a.x)(b.y - a.y) - (P.y - a.y)(b.x - a.x) with e = |dx| (|ty| + eP.y) + |ty dx| + |dy| (|tx| +
  eP.x) + |tx dy| + |v| (uncertain: 0 when e = 0, else exact through slot 0: (X - a.x W) dy - (Y - a.y W) dx with
  the facet values as SuperInteger<1> (FUN_180026d90) -> SuperDouble); any o_i > 0 -> 0; all three non-zero -> 3,
  one zero -> 2, two zeros -> 1. Code 3 -> the point is covered (record cleared); 1 / 2 -> the facet is pushed on the
  point's list. Then per record (+0xa8 from the head): covered -> nothing; no facet or one facet -> TestPointPassed;
  >= 2 facets -> a Shell2D (FUN_1800043f0(2^-20)) of those facets' vertices (cached per facet node, FUN_180004840)
  and edges (FUN_1800049a0), FUN_180008ed0, FUN_180004a50, and PASSED when FUN_180021830 holds for an edge of the
  output list +0x40 (= the point lies on the closed segment of that periphery edge: FUN_180021960 side of the
  edge's line, then FUN_180022260 along the axis against both ends); FUN_180007cc0 resets the Shell2D.
- Mode 1: FUN_180014f70 tree; per test point in order: passed -> FUN_1800151f0 (curve appended to the tree list,
  its edges (from the curve's head) pushed front on their nodes' point copies' +0x48), else FUN_18002a2c0.
- The i11 Shell2D port (ifk_sil_shell2d.c) reuses the ifspinsil port's computation only where the functions were
  checked to be the same (table in its header); different and ported from i11: FUN_180003cb0 (error-bound summation
  order), FUN_180003250 (own stamp, marks components visited), FUN_180005b10 (equal-vertex swap when the extreme has
  no edges; candidates from the extreme's head edge groups only), FUN_180004a50 (a walk reaching a component already
  done is discarded; split loop twice, disassembly 180004cc3). Correction to notes_i11_silhouette.md sec. 3:
  FUN_180004840 does track the extreme vertex (inlined).

#### 11.8.4 Polygon2D assembly, nesting, output (FUN_1800152c0) [code]
- Reference node: a CutCurveNode on the stack with a fresh point (FUN_18000a1b0). For each tree curve, for each of
  its edges (from the head) with +0x10 = 0 and +0x11 = 0: FUN_180015b30 = a BFS over curve edges from that edge (+0x11
  = 1): per popped edge (pushed front on the CutPolygon's [1]) Polygon2D nodes for both curve nodes (cached at node
  +0x28, FUN_18003cd10: box), a Polygon2D edge (FUN_18003cdc0; reversed when +0x11 = 2), then at its "to" node every
  unvisited curve edge: one-way ones only when they start there (+0x11 = 1), two-way ones with +0x11 = 1 (start
  there) or 2. Queue empty -> FUN_18003cec0 close, CutPolygon [6] = the node of the head edge's [0].
- FUN_18003cec0: +0x38 = 0, box min x / y -= 2 tol (tol = 1e-7, Polygon2DMemory +0x38), "Polygon2D error 5 / 4: open
  polygon" for a node of degree < 2, <= 100 edges: one quadtree cell with all edges (else Polygon2DQuadtree, 11.8.6);
  the reference node is set to (min x, min y) (slot 4 FUN_180012d90) and when FUN_18003d480 says it is INSIDE, every
  edge is reversed (result byte 1).
- FUN_18003d480(polygon, node): outside the box by more than tol -> +0x38 (0). Else the first edge (from the head)
  whose [0] node has degree 2 and passes FUN_18003dba0 (the node's two edges e1, e2: s = side of e1's far end
  against e2; the tested node must lie on neither line; flag from the signs) -> ray (node, that vertex),
  FUN_18003d6e0 counts crossings with every edge once (stamp): FUN_18003ddc0 filtered (FUN_18003e060 same side,
  FUN_18003e0f0 collinear / touching cases with +0x28 / +0x29, FUN_18003e2a0 vertex hit -> +0x30), uncertain pairs
  deferred to FUN_18003e340 (the same with slot 2 exact orientations); a vertex hit counts the far ends of that
  vertex's edges strictly left of the ray (FUN_18003e430, once per vertex). Inside = odd count (flag 0) / even
  (flag 1). CutCurveNode slots: 0 = (+8, +0x10), 1 = FUN_180012480 filtered orientation (t - a) x (b - a) on the
  point copy's doubles, 2 = FUN_1800126c0 exact (FUN_180012dc0: exact record from the head cut node, or from the
  doubles with D = 1 when the point has none = the reference node), 3 = FUN_180012b20 compare along an axis.
- FUN_180015d20 nesting: first, roots contained in the new polygon P move under it (FUN_180015f10, test = P contains
  Q's [6] node). Otherwise: the leaf list (polygons without children) is searched first, then the roots, then the
  chosen parent's children, descending; two containers at one level -> "PlaneCut error 47: multiple parents.";
  P is pushed front on its parent's children (the parent leaves the leaf list) or on the roots; a P without
  children goes on the leaf list. Exact records made during the tests are freed (FUN_180015fc0; no effect).
- Output: slot 0; per polygon in creation order: slot 1 (tree node) unless it has one, its parent's node, slot 2
  (link); node caches (+0x28) cleared; then per polygon its curve edges popped from the head: slot 3 once per curve
  node (x, y, z; cached at +0x28 across polygons), slot 4 (a, b, edge +0x10, tree node: the 5th argument is dropped
  by the decompiler, [inferred] from FUN_180024bc0 reading param_5 as the tree node); slot 5 (orphans under the root).
  Note: output edges keep the curve-edge direction; the Polygon2D reversal only serves the inside tests.
- `IfkPcTree`: tree nodes in slot-1 order with parent (-1 = root), children in the DLL's list order, points (x, y, z),
  edges (a, b, flag, node).

#### 11.8.5 Self-test (tools/tests/ifk_planecut_test.sh, stage B part; meshes generated in the driver)
```
tree: box -> one square loop: 1 tree node(s), 4 points, 4 edges; curves 1, test points 1 (Shell2D 1), angle-sorted points 0: PASS
tree: cylinder standing (axis Z, 24 facets) -> one 24-gon: 1 tree node(s), 24 points, 24 edges; curves 1, test points 1 (Shell2D 1), angle-sorted points 0: PASS
tree: cylinder lying (axis X, 24 facets) -> its rectangle outline: 1 tree node(s), 30 points, 30 edges; curves 1, test points 1 (Shell2D 1), angle-sorted points 0: PASS
tree: UV sphere 32 x 16 -> the equator 32-gon: 1 tree node(s), 32 points, 32 edges; curves 1, test points 1 (Shell2D 1), angle-sorted points 0: PASS
tree: box with a vertical through hole -> outer square + the hole as its child: 2 tree node(s), 8 points, 8 edges; curves 2, test points 2 (Shell2D 2), angle-sorted points 0: PASS
tree: two overlapping boxes -> one loop, the union outline: 1 tree node(s), 8 points, 8 edges; curves 1, test points 1 (Shell2D 1), angle-sorted points 0: PASS
tree: small box on a big box -> the big square only: 1 tree node(s), 4 points, 4 edges; curves 2, test points 2 (Shell2D 1), angle-sorted points 0: PASS
tree: two boxes touching at a corner -> the 8 square edges (angle sort at the degree-4 point): 1 tree node(s), 7 points, 8 edges; curves 2, angle-sorted points 1: PASS
```
Each loop is checked to be one closed cycle on exactly the expected snapped vertex set, plus the nesting. The lying
cylinder's short sides carry 15 points each: snapping (floor to 2^-20) separates 2 cos a and 2 cos -a where they
differ in the last bits (e.g. y = 0 and y = -2^-20). The corner case is one tree node (FUN_180015b30 follows
outgoing edges through the degree-4 point, so both squares form one Polygon2D). Also run once under ASan / UBSan:
clean. ifk_s2_test (the Shell2D side path alone, 8 cases): ALL PASS. NOT checked against Mastercam (N11-5).

#### 11.8.6 NOT DECODED / not ported / inferred
- Not ported: more than one cut plane in `ifk_pc_run` (stage A handles several; stage B keeps the plane's records
  alive until the output), modes 0 / 2 (FUN_18002c810, FUN_18002cb50, FUN_180030240), flipped sets (the edge reversal
  after slot 10), the buffered branch of FUN_180029630 (FUN_180003d90), FUN_180012f50's node scan and exact branch
  (unreachable with ground -DBL_MAX; reported if reached), Polygon2DQuadtree for > 100 edges (FUN_18003cc70 /
  FUN_18002fee0 / FUN_18003eaa0 query: replaced by all edges [inferred: the query only proposes candidates and every
  edge is tested once by stamp]), the PlaneCutToSilhouette test-point quadtree (FUN_180027af0 / FUN_180027bd0 /
  FUN_180008b60: replaced by every uncovered point [inferred: FUN_180020c10 rejects outside the facet box itself;
  the codes are exact facts]), the Shell2D EdgeQuadtree (ifk_sil_shell2d.c header).
- [inferred]: FUN_18003e0f0's first slot-3 call passes axis 0 (third argument dropped by the decompiler; the
  second call passes 1); FUN_18003dba0's first slot-1 call takes (e1 [0], e1 [1]) (arguments dropped; read off its
  exact fallback); slot 4's tree-node argument (above); FUN_18003b3a0's AVL is ported as decompiled, the in-order
  does not depend on it, only which pairs the exact path compares (side effects on point doubles).
- FUN_1800160c0 (double -> SuperDouble for the reference node) is taken as the exact conversion (not read).
- Shell2D side path: i11 continues with a NULL edge after "Shell2D error 6" (the port returns -1 from
  ifk_s2_process); FUN_180021830's error-5 path (axis -1) not ported; "IFSilhouette checkpoint 1" (FUN_180022260,
  dialog slot +0x40) only counted.
- Pool order: stage B frees through the emulated point pool (pt_free) like the DLL; nothing after FUN_180029630 was
  found to depend on record addresses.

### 11.9 Stitch2D, output, ifk_silhouette_run (2026-11-12, stage C)
Code: `viewer/ifk/ifk_silhouette.c` / `.h` (`ifk_silhouette_run(mesh, tol, out_path, &result)`; every step cites its
function), `ifk_cli --silhouette MESH TOL OUT`, self-test `viewer/ifk/ifk_silrun_test.c` run by
`tools/tests/ifk_silhouette_test.sh`. New dumps (decomp/interfacial, not in git): i01_surfer_all.c (every Surfer::
method of interfacial01 + callees), i11_vt_stitch2d.txt (Stitch2D / Octree / PlaneCutToSilhouette vtables).
Hand-over that led here: decomp/interfacial/notes_planecut_stageB.md.

#### 11.9.1 The post-PlaneCut loop (Run l.24440-24760) [code]
- Structures. PlaneCutToSilhouette (FUN_180024830): +8 points (push-front), +0x10 edges (push-front), +0x18 root
  tree node, +0x20 pending tree nodes, pools +0x30 CurvePoint (x, y, +0x10 = 0 at allocation), +0x38 CurveEdge
  (a, b; slot 4 drops the flag), +0x40 TreeNode (0x58 bytes: [0] parent, [1] union-find link, +0x10 edges
  (push-front), [3] +0x18 counterpart, +0x20 / +0x28 children head / tail). Slot 2 FUN_180024af0 pushes the child
  front on the parent's list, slot 5 FUN_180024c40 hangs orphans under the root.
- Breadth-first over the first tree from its root (FIFO of list nodes). For a node X with children: per child (list
  from the head), per edge of the child (+0x10 from the head), per end ([0] then [1]): a CurvePoint whose +0x10 is 0
  gets a Stitch2D point (x, y, 0.0; FUN_180013ce0, pushed front on Stitch2D +8, box) and a StitchPointOutput record
  (FUN_180020590: [1] = the child, at point +0x28) and the CurvePoint's +0x10 = that point; then a Stitch2D edge
  (FUN_180013ef0) of the two points pushed front on +0x10. FUN_180012340(stitch, tol), FUN_180012e00(stitch).
- Union-find: for each unflagged Stitch2D edge (+0x10 from the head): ra / rb = roots (via [1]) of the nodes of the
  records of [0] / [1]; ra != rb -> rb[1] = ra and rb's children appended to ra's (tail kept).
- Rebuild: for each unflagged edge (same order): R = root of [0]'s record node (the record's node becomes R);
  R +0x18 == 0 -> a new TreeNode of the second tree (pushed front on its pending list), R +0x18 = it, its parent =
  (root of R's first-tree parent) +0x18, or the second root when that is 0 (the first root's +0x18 is 0), pushed
  front on the parent's children. A record without second-tree point gets one (x, y of its Stitch2D point); the edge
  (a, b) is pushed front on the global list and on the node's list.
- Then every child of X is popped: its edge list freed with both CurvePoints' +0x10 = 0; a child without union-find
  link is appended to the queue. FUN_180012260 resets the Stitch2D. After the loop: FUN_1800204a0, the pending list
  (every node already has its parent), dialog slot +0x10, FUN_180025000(tree, tol x tol), FUN_180024da0.
- Effect: siblings joined by an edge after stitching (shared or snapped points, cancelled overlaps) become one tree
  node, their children are merged; points are re-inserted per level (a point shared with a child level is a new
  Stitch2D point there).

#### 11.9.2 Stitch2D FUN_180012340(stitch, tol) and FUN_180012e00 [code; octree replaced, see below]
- Octree<Stitch2D::Point> (FUN_180014280; root box = Stitch2D box - 5 tol, edge = largest extent + 10 tol; build
  FUN_180014360 / FUN_180016f70; split rule FUN_180014670). Every point gets an OctreePoint (FUN_180013540).
- Representatives FUN_180012a70(stitch, P, octree, tol^2) for each point P of +8 from the head: P +0x18 == 0 ->
  P +0x18 = P, and every point Q in the cells the box P +- tol touches (OctreeBox slot 0 FUN_180011820: a side is
  "beyond" when the distance exceeds tol strictly) with Q +0x18 == 0 and (Qy-Py)^2 + (Qx-Px)^2 + (Qz-Pz)^2 < tol^2
  gets Q +0x18 = P. No transitive growth: a representative is never within tol of an earlier one.
- Edge loop (pop +0x10 from the head): [0] / [1] = their representatives; equal -> dropped (Probe slot 0 =
  FUN_180002bf0, empty; Stitch2D +0x50 = &Probe, so the edges' +0x18 is never written). Else FUN_180012ba0(stitch,
  E, octree, tol, tol^2, &list): both ends stamped (DAT_1802682d8 += 2), the query prism FUN_1800119b0 (cross-section
  +- tol around the segment, OctreeEdgePolygon slots), and for every point of the cells found its REPRESENTATIVE,
  when not stamped: stamped and kept (pushed front) when FUN_180011660 holds: t = d.(R - A) strictly between 0 and
  |d|^2 and the squared distance to the foot (d t / |d|^2 + A) < tol^2. No point kept -> the edge is pushed front on
  the new list. Else each kept R gets the key (R - A).(B - A) in a binary tree (smaller key left, equal keys right;
  FUN_18000b0c0 / FUN_18000b200), and the pieces (A, R1), (R1, R2), .., (Rn, B) in ascending key order are pushed
  front (the original edge freed). The new list becomes +0x10 (so: reverse processing order); pieces are not split
  again. FUN_180014e30 / FUN_1800129e0 free the octree.
- FUN_180012e00: per unordered point pair a tree record with two stacks (edges from the smaller pointer, the
  others). Walking +0x10 from the head: a new pair -> pushed on its stack (flag 0); else when the opposite stack is
  not empty, the edge and that stack's head both get flag (+0x10) 1, otherwise the edge is pushed on its own stack.
  So opposite edges cancel pairwise; same-direction duplicates stay.
- Port: the octree is replaced by a list of representatives / points sorted by x with a window of 2 tol + 64 ulp of
  the coordinates around the point / segment (every point those exact tests can accept lies inside: rounding of
  non-negative sums is monotone) [inferred-equivalent: the octree only proposes candidates]. The DLL's candidate
  order (cell traversal) only matters for split points with exactly equal keys on one edge: NOT DECODED, the port
  uses the point-list order and counts such ties (`nties`). FUN_180012e00's pointer keys: only equality and a
  symmetric orientation are used, so the result is pointer-independent.

#### 11.9.3 FUN_180025000(tree, tol^2) with FUN_180024600 [code]
Breadth-first from the second root's children (list order). A node A with exactly one child B that has no children,
where FUN_180024600(A, B's edges) and FUN_180024600(B, A's edges) hold, is unlinked from its parent's children and
A and B are freed; otherwise A's children are appended. FUN_180024600(X, Y edges, tol^2): for each edge of Y (from
the head), for [0] and [1]: the minimum over X's edges of the point-segment squared distance (s = b - a, d = p - a,
L = sy sy + sx sx + 0, t = dy sy + dx sx + 0, D = dy dy + dx dx + 0; t > 0 and t <= L: D - t^2 / L clamped at 0;
t > L: |p - b|^2; else D); a minimum > tol^2 -> 0. All within -> 1. Points and edges of the removed pair stay on the
global lists but are never reached by the writer.

#### 11.9.4 FUN_180024da0 through Surfer, Surfer::WriteCurves binary [code]
- Surfer impl (interfacial01): +0x18 node count, +0x1c edge count, +0x40 / +0x48 node list (append), +0x50 / +0x58
  edge list (append), +0x7c tree flag, +0x7d z-cut mode (FUN_180013d70 sets 0; only Surfer::SetZCutMode sets it,
  Run never calls it), +0x80 tree-node counter. NewWriteCurves(true): +0x7c = 1, FUN_180017810(.., 1) clears nodes,
  edges, both counts (one 8-byte store at +0x18) and tree nodes. AddCurveTreeNode: id = ++(+0x80), pushed front on
  +0x88. AddCurveTreeBranch(parent, node): node +8 = parent. AddWriteCurveNode(x, y, z, false, -1): FUN_180003990,
  id +0x18 = ++(+0x18), appended. AddWriteCurveEdge(a, b, false, tree) -> FUN_180017a20: appended to +0x50, pushed
  front on both nodes' +0x20 lists, +0x12 = false, +0x1c += 1, both degrees (+0x2a) + 1, +0x18 = tree node.
  **EndWriteCurves = export RVA 0x19d0, an empty function** (shared with EndWriteSurface / EndIntermediateSurface).
- FUN_180024da0: breadth-first from the second root; at a popped node X each child (list from the head) gets a Surfer
  tree node (AddCurveTreeBranch with X's Surfer node, NULL for the root), then per edge of the child (from the head)
  a Surfer node for each end whose CurvePoint +0x10 is 0 (stored there) and AddWriteCurveEdge. Then every child is
  popped: its edge list freed with both ends' +0x10 = 0, the child appended. So a point used on two levels is
  written twice (once per level); within one level it is shared.
- FUN_180017b20(surfer, path, binary = 1, param_4 = 0, table, buffer, param_7 = 0): fopen "wb"; header `binary tree
  curves` (18 bytes, +0x7c set; "binary curves" 13 without tree; the 20 / 25-byte "tagged" headers only with
  param_7); int32 +0x18, int32 +0x1c; the nodes in list order, 24 bytes each (x, y, z); with param_4 false each node
  is checked: +0x28 == 0 and degree < 2 -> "Surfer warning 59: hanging node detected in group -1", odd degree > 1 ->
  "Surfer warning 29: odd junction detected in group -1" (messages only). Then for each edge of +0x50 in order,
  while it is not visited: FUN_180019490 picks the start, FUN_180019880 writes one curve.
- FUN_180019490(surfer, e): stamp DAT_1800748e8 += 2; the component of e over unvisited edges, breadth-first ([0]
  then [1], each node's +0x20 list from the head); start = the first component edge with +0x12 == 0 whose [0] node
  has degree 1; (the z-cut branch is skipped: +0x7d = 0); else the first with +0x12 == 0 = e itself.
- FUN_18001a430(surfer, start, &list): start appended (+0x10 = 1, +0x11 = 0); at the current end node take the
  first unvisited edge of its +0x20 list (from the head) that starts there (the "+0x12 both ways" fallback never
  applies here); tree mode: its tree node differs from the previous edge's -> "Surfer error 62: conflicting tree
  groups." (dialog +0x40 with 1); appended, reversed flag +0x11 = 0 (1 only for a +0x12 edge entered from its end);
  stop when there is none. Returns the count.
- FUN_180019880 binary: int32 count; tree mode: int32 = the first edge's tree node id, int32 = its parent's id (the
  4 bytes at node +8 = NULL when there is none: 0); per edge 8 bytes = the two node ids, swapped when +0x11 is set;
  param_7 (tag byte +0x13) is 0. Every byte goes through FUN_180016350 (table at param_5; plain fwrite without
  one). The text branch ("curves" / "tree curves" lines, %g nodes, "segments") is not used by Run (binary 1).
- Result: the file layout equals CurveXferMC's format 1 (9.2) except the header text (space instead of `_`):
  `binary tree curves` | int32 nnode | int32 nedge | nnode x 3 doubles | per curve: int32 n, int32 tree id, int32
  parent id, n x (int32 a, int32 b) 1-based. **ifk_read_bin_curves(format 1) reads it unchanged** (it checks only
  byte 0 = 'b'; GetDataFileFormat sees 't' at byte 7 and 'c' at byte 12). The two extra int32 per curve are the tree
  ids, so mcmsurf's +0x144 / +0x148 = (tree node, parent) [code on the writer side].
- IFSilhouette's post-processing FUN_180026a40 maps the table bytes back (11.4): the port writes the plain bytes.

#### 11.9.5 ifk_silhouette_run [code]
Blob as IFSilhouette packs it (11.1); |tol - 0.1234567| <= 1e-6 -> IFK_SIL_TOLCHECK (Run l.24232 shows the message
and calls FUN_180199d54(0) = exit); input stage (ifk_silhouette_walls; overflow = "IFSilhouette error 2" with dialog
argument 1 -> IFK_SIL_OVERFLOW); PlaneCut with grid 2^-20 (PlaneCut::PlaneCut(.., DAT_180268368)), the cut-plane
set (0, 0, 1) at 0.0, the walls, EndInput(true), ifk_pc_run(mode 1) with ifk_sil_classify; 11.9.1-11.9.4. The
result struct carries the second tree, the file content (nodes, curves with tree ids) and counters (stitch levels,
moved points, dropped / split edges, ties, cancelled pairs, unions, removed pairs, warnings 59 / 29). Messages with
dialog argument 1 (error 2, Surfer error 62) are treated as stopping the run: what the IFSilhouette dialog does with
them is NOT DECODED [inferred].

#### 11.9.6 Self-test (tools/tests/ifk_silhouette_test.sh, stage C part; meshes made in viewer/ifk/ifk_silrun_test.c)
Each written file is read back with ifk_read_bin_curves(format 1); every curve must be one closed chain on exactly
the expected snapped vertex set, with the exact bounding box, |area| within 1e-9 relative, tree parent ids as
expected, z = 0, header text and file size exact. Output:
```
box 3 x 2 x 1.5 -> one rectangle: rc 0, file nodes 4 segs 4 curves 1; stitch levels 1 moved 0 degenerate 0 split 0 cancelled 0 unions 0, removed pairs 0, warnings 0/0: PASS
cylinder standing (axis Z, r 2, 24 facets) -> the 24-gon: rc 0, file nodes 24 segs 24 curves 1; stitch levels 1 moved 0 degenerate 0 split 0 cancelled 0 unions 0, removed pairs 0, warnings 0/0: PASS
cylinder lying (axis X, 24 facets) -> its rectangle outline: rc 0, file nodes 30 segs 30 curves 1; stitch levels 1 moved 0 degenerate 0 split 0 cancelled 0 unions 0, removed pairs 0, warnings 0/0: PASS
UV sphere r 3, 32 x 16 -> the equator 32-gon: rc 0, file nodes 32 segs 32 curves 1; stitch levels 1 moved 0 degenerate 0 split 0 cancelled 0 unions 0, removed pairs 0, warnings 0/0: PASS
box 4 x 4 with a 1 x 1 through hole -> outer square + the hole as its child: rc 0, file nodes 8 segs 8 curves 2; stitch levels 2 moved 0 degenerate 0 split 0 cancelled 0 unions 0, removed pairs 0, warnings 0/0: PASS
two unit boxes 2^-12 apart, tol 1e-3 -> stitched into one loop (6 segs, area 2 + 2^-12): rc 0, file nodes 6 segs 6 curves 1; stitch levels 1 moved 2 degenerate 0 split 0 cancelled 1 unions 1, removed pairs 0, warnings 0/0: PASS
box + smaller box 2^-12 off its side, tol 1e-3 -> edge split at the T-junctions, one loop of 8: rc 0, file nodes 8 segs 8 curves 1; stitch levels 1 moved 0 degenerate 0 split 1 cancelled 1 unions 1, removed pairs 0, warnings 0/0: PASS
square tube, wall 2^-12 < tol 1e-3 -> zero-width pair removed, empty file: rc 0, file nodes 0 segs 0 curves 0; stitch levels 2 moved 0 degenerate 0 split 0 cancelled 0 unions 0, removed pairs 1, warnings 0/0: PASS
square tube, wall 2^-6 > tol 1e-3 -> outer + hole kept: rc 0, file nodes 8 segs 8 curves 2; stitch levels 2 moved 0 degenerate 0 split 0 cancelled 0 unions 0, removed pairs 0, warnings 0/0: PASS
two boxes touching at a corner -> 8 segments in closed curves, total area 2: rc 0, file nodes 7 segs 8 curves 1; stitch levels 1 moved 0 degenerate 0 split 0 cancelled 0 unions 0, removed pairs 0, warnings 0/0: PASS
tol 0.1234567 -> Run's tolerance check (l.24232), nothing written: PASS
ALL PASS
    rc 0 tree nodes 2 (removed 0) file nodes 8 edges 8 curves 2 msg ""
    stitch levels 2 points 8 moved 0 degenerate 0 split 0 ties 0 cancelled pairs 0 unions 0 warnings 59/29 0/0 planecut messages 0
PASS ifk_cli --silhouette (block with a hole as STL) -> 2 curves, the hole under the outer loop
```
The basic five use tol 1e-7 (below the 2^-20 grid: no stitching, the exact PlaneCut loops); the stitch cases use
1e-3. Outer loops come out counter-clockwise and holes clockwise here (the curve-edge direction, 11.8.4). Also run
once under ASan / UBSan: clean. NOT checked against Mastercam (N11-5).

#### 11.9.7 NOT DECODED / inferred (stage C)
- The octree's candidate order (ties of split keys only; counted). The octree itself replaced (above).
- What the dialog does after a message with argument 1 ("IFSilhouette error 2", "Surfer error 62"): the port stops.
- Surfer::ReadFile's AddNode / AddTriangle order (input array order assumed, as in 11.6).
- Not ported: Run's route 1 (Stitch -> Trip), Surfer::CompressCurves (param_4 false), the memory-buffer output
  (BinaryCurveFileSize, empty output path), the text curve format, the byte table (out of scope).

## 12. The STL the kernels get: wr_stl -> surf_to_stl -> make_surf_polygons (decision N12-1, 2026-11-12)

Port: `viewer/ifk/ifk_surfpoly.c` / `.h` (`ifk_wr_stl`, `ifk_surf_to_stl`, `ifk_msvc_f`) on `viewer/mcpartline_mesh.c`
`plm_mesh` (the make_surf_polygons port of Curve Parting Line, formats/wireframe.md "Curve Parting Line"); tool
`viewer/ifk_stl_port` (make target). Dumps (decomp/interfacial, not in git): mccore_wr_stl.c (wr_stl, surf_to_stl,
make_surf_polygons, depth 2), surf_to_stl_asm.txt (DisasmAt 1806d22e0..1806d2e20), sld_stl.c (mcgeomsld: sld_to_stl,
the solid-face temp file, depth 1), sld_stl_go.c (mcgeomsld FUN_18003b070, FUN_18003cbc0, FUN_180052730), msp_trim.c (the trimmed
path's callees not in decomp/partline, 12.4). The mesh engine's dumps are decomp/partline/ (core_tess.c FUN_18057a980 etc.).

### 12.1 wr_stl (mccore 1806d2e60) as mucbMakeIffSiluetBndy calls it [code]
- Call (10.1): `wr_stl(tol = dat+0x10, file, "SILBNDRY.STL", -1, 1, 1, 0, param_4, 0)`: filter -1, ASCII, quiet
  (DAT_181430b30 = 1: no status text, no warnings), no view matrix, param_8 = entities on invisible levels too.
- GetProdEnableBits & 1: the file must be in the temp directory (reduce_filename / _mbsicmp), else nothing is written.
- The matrix: local_2c18 = identity, origin local_2bd0.. = 0 (param_7 = 0: the active view and origin are not read).
- First walk (get_raw_ent class mask 0x110100: surfaces 0x100, solids 0x10000, polygon meshes 0x100000): an entity is
  taken when ((sel & 0x100) == 0 (not blanked) and (LevelData::isVisible(level) or param_8)) or param_9, and (filter 0,
  or filter -1 and sel bit 4 (selected), or ent +0x34 (ent_idn) == filter). It looks for one usable entity (a solid only
  when its ent +0x70 bit 2 is clear; a polygon mesh of kind (ent +0x68) 3 does not count); none and no solid faces in
  the temp file (sld_count_temp_file(1) == 0): warning 0x153b unless quiet, nothing written.
- Else: "solid ascii\n" (binary: the 80-byte "MASTERCAM STL DATA" header + count), edge_setup_save, init_poly_space(0),
  then the second walk in database order with the same filter: surfaces -> surf_to_stl, solids -> sld_to_stl (12.3),
  polygon meshes -> copyPmeshToStlFile (only mesh kind 0: FUN_1806cf480, MCLoadSTLMesh); after the last entity (filter -1 only) the
  selected solid faces: rewind_solid_surfs_temp_file, read_next_solid_surf_from_temp_file -> surf_to_stl (12.3). A set
  Esc flag (make_surf_polygons' param_11) ends the walk. "end solid\n" (binary: the header and count rewritten, the
  entity colour text "ENTITY COLOR:#%02X%02X%02X" at byte 0x3c when it is 20 characters long).
- Ported (ifk_wr_stl): the ASCII text for surfaces; the caller does the entity filter. Not ported: binary STL (not used
  by these commands), solids, polygon meshes, the solid-face temp file.

### 12.2 surf_to_stl (mccore 1806d22e0) [code; floating-point order from surf_to_stl_asm.txt]
- `make_surf_polygons(1, 0, ent, tol, 0, draw, &tri, &xyz, NULL, &err, &esc)` (1806d23c8); err, esc or no triangles:
  0 facets. make_surf_polygons(1, 0, ..) differs from Curve Parting Line's (1, 1, ..) only in the uv output: it sizes the
  arena (FUN_18057a980 core_tess.c l.1145 / 1161 / 1185 / 1738: edge_fit / get_storage of the uv block) whose overflow
  make_surf_polygons' retry loop (FUN_1806fb740) absorbs; the triangles are the same (checked: 12.5).
- Vertices 0 .. max index (the largest index of any triangle) through the matrix in place (1806d2442..1806d2804, the
  top 4 at a time then the rest, the same formula): x' = (x M0 + y M3) + z M6 (y', z' with M1 M4 M7 / M2 M5 M8), then
  + the origin. With the identity of 12.1 the values are unchanged except -0.0 -> +0.0 (0.0 + -0.0).
- Per triangle (a, b, c): B = b - a, C = c - a; n = (Cz By - Cy Bz, Bz Cx - Cz Bx, Cy Bx - By Cx) (1806d289b..1806d28c1);
  l = sqrt((nx^2 + ny^2) + nz^2); l == 0 -> n = (+0, +0, +0) else n / l (a NaN length divides). Surface flag ent +0x6e
  bit 2 (normal reversed, formats/mcam.md): n negated (sign bit flipped) and the corners written a, c, b.
- Every value converted to float (CVTPD2PS) and written: ASCII "  facet normal %f %f %f\n", "    outer loop\n",
  three "      vertex %f %f %f\n", "    endloop\n", "  endfacet\n" (the floats widened back to double for the
  varargs); binary: the 4 x 12 bytes and a 2-byte 0 attribute. Returns the triangle count.

### 12.3 Solids and solid faces [code; the faceting itself NOT DECODED]
- A whole solid (class 0x10000) goes to sld_to_stl (mcgeomsld 1800ce5e0) -> ps_stl_output(body tag = ent +0x68, tol,
  ..) (180052580): render options FUN_180052730(tol) (option block of 0x168 bytes: tolerance tol / 39.37007874015748
  (inch) or tol / 1000 (metric) at +0x48 and +0x78 (the unit change to Parasolid's metres), pi / 2 at +0x68, 1000.0 at
  +0x38 / +0x58, flags and Parasolid option tokens 0x5016 ..), then **PK_TOPOL_render_facet** (Parasolid): the triangles
  are Parasolid's. They arrive in the GO callback FUN_18003b070 ("_stl_write_poly", PARASOLI.CPP): the points scaled
  back by 39.37 / 1000, through the x_matrix, written with the same STL lines. **Parasolid's faceter is a third-party
  kernel (pskernel): NOT DECODED, not ported** - Turn Profile / Silhouette Boundary of a selected solid cannot match
  Mastercam without it.
- Selected solid faces (sld_face_to_temp_file 1800ee210, read back after the database walk): with
  `convert_solids_to_surfaces` == 0 only a 0x38-byte header (face tag, no surface: DAT_1803e8ac4 = 0) is written, and
  read_next_solid_surf_from_temp_file (1800ecd50) returns 0 at the first such header: no face reaches surf_to_stl.
  With it set, ps_face_to_mc_surf (180045200: Parasolid face -> Mastercam surfaces of kind 0 / 10, PK_* calls, NOT
  DECODED) writes each surface (FUN_1800f00e0: header, the 0x2938-byte ent and its data; a trimmed surface's base first
  with the "take" flag clear) and FUN_1800ed1c0 reads them back for surf_to_stl. Who sets convert_solids_to_surfaces
  for these commands: NOT DECODED.

### 12.4 make_surf_polygons on the surface kinds
- Kind 0 (untrimmed NURBS): ported bit-exact (plm_mesh; wireframe.md "Curve Parting Line" make_surf_polygons).
- **Degenerate kind 0 surfaces** (both v rows of the net equal, e.g. 4 records of ROUGHPROJNCI): calc_surf_norm fails
  inside the offset evaluation FUN_180460cd0, eval_surf returns non-zero and leaves its output unwritten, and
  FUN_180581f00 does not test the return (core_tess.c l.3826..4470: none of its eval_surf calls is checked): the steps
  are computed from stale stack values. The result depends on what earlier calls left on the stack: NOT DECODED
  (not reproducible in principle); the port refuses these surfaces ("calc_surf_norm failed in the offset evaluation").
- Kind 10 (trimmed), outline read from FUN_18057a980 [code; ported 2026-11-12 over kind 0 bases, 12.8]: find_base_surface gives the base; FUN_180591650
  reads the trim loops (uv points, loop table local_2d68) and their uv box; the grid is made on the base as for kind 0;
  FUN_1805937e0, FUN_180577130 (the loops prepared), FUN_180576e90 (fails -> the flag local_2c48 (in_stack_f8) is set and
  no triangles are made), with in_stack_90 (= tol x 0.1) > 0 FUN_18057f9a0 + FUN_180598070 refine the loop
  points (FUN_180598d40 per point), in_polygon nests them, FUN_180577620 orders them, FUN_18057dd40; the loop points are
  evaluated as extra vertices (FUN_18057a510 per loop point, after the grid); per grid cell the cell rectangle (4
  corners, vertex indices b, b + nv + 1, b + nv + 2, b + 1) goes through FUN_1805913b0 (the cell against the loops) and
  FUN_180592030 (the triangles of the clipped cell, writing into the triangle array; its callees FUN_180586a50 etc.).
  The static callees (decomp/partline/core_tess.c; the next port's scope): FUN_180591650 -> FUN_180586880,
  FUN_180586a50 (-> FUN_18058a7c0, FUN_180592ce0, FUN_18000f270), FUN_180598070 (-> FUN_1805776f0, FUN_180577e10,
  FUN_180578340, FUN_180579660), in_polygon, is_bdy_ccw; FUN_180577130 -> FUN_180599630; FUN_180576e90 ->
  FUN_180592ac0, FUN_180598d40, FUN_18059bb90; FUN_18057f9a0 -> FUN_180580470 (-> FUN_180581710), FUN_1805812e0,
  FUN_180581410, FUN_18058a980 (-> FUN_18058b700); FUN_180577620 -> is_bdy_ccw; FUN_18057dd40 -> FUN_18058e180;
  FUN_1805913b0 -> FUN_180592c60, FUN_180592a90, FUN_1805909d0, in_polygon; FUN_180592030 ->
  FUN_180592ce0, FUN_180592f60 (-> FUN_18058aea0, FUN_180599690, FUN_1805999f0). The callees missing from core_tess.c are
  dumped in decomp/interfacial/msp_trim.c (31 functions, 2590 lines, depth 1). About 5000 decompiled lines in all, and
  the engine's stack arguments must be read from the disassembly (as for the kind 0 port).
  The port, its quirks and its oracle results: 12.8.
- Kinds 1, 2, 4, 6, 7, 8 (get_evaluation_grid's other cases, eval_surf's other evaluators), and kind 10 over a base of
  those kinds (corpus: 1305 over kind 1, 391 over kind 2, 21 over 6, 9 over 8): NOT PORTED (each needs its evaluator
  bit for bit: kind 1 FUN_1805eb9c0 / FUN_1805ec000, kind 2 eval_revsurf, kind 4 FUN_18042cad0, kind 6 FUN_180412490,
  kind 7 FUN_18041c730, kind 8 FUN_180460cd0 with a non-zero offset, and init_surf_const's cases for the grid points).
  2026-11-14 (N13-33, hst_linker.md 21.5): eval_surf kinds 1 (FUN_1805eb9c0 / FUN_1805ec000 + eval_poly_deriv) and 8 (FUN_180460cd0 with
  find_base_surf) are ported bit for bit (mcpartline_mesh.c pp_eval / plm_eval) and used by make_surf_model (plm_model: corpus 560 kind 1 /
  10-over-1 and 8 kind 8 / 10-over-8 surfaces = mccore.dll); make_surf_polygons (plm_mesh, this section's path) still takes kind 0 / 10-over-0
  only (its grid points go through init_surf_const / eval_surf_const, whose kind 1 case FUN_1805ebcd0 is not ported).

### 12.5 MSVCR120 "%f" [code]
SysFile::Printf (180a3b2a0) = vfprintf_s of MSVCR120 on the FILE (+0x10). Its %f: $I10_OUTPUT 18 digits rounded
half-up to 17, then _fptostr rounded half-up at the 6th decimal (the double rounding of viewer/mp/mp_fmt.c's
disassembly notes), sign = sign bit: 0.0078125 -> "0.007813" (glibc: 0.007812), -0.0 -> "-0.000000", -1e-9 ->
"-0.000000". Port: ifk_msvc_f.

### 12.6 The oracle (tools/peloader --stl / --crtf)
- `tools/peloader/mccore_stl.c`: mccore.dll's own wr_stl (and make_surf_polygons(1, 0, ..) per surface) on part
  records; get_raw_ent / get_ent_from_eptr answered from the records, SysFile::Printf / Write hooked (each %f printed as
  its exact double, %a), level visibility 1, GetProdEnableBits 0, no solid-face temp file. `tools/peloader/crt_fmt.c`:
  the user's own msvcr120.dll (Windows copy, read only) loaded with its DllMain, its sprintf("%f") called (win32.c got
  GetCommandLineW, the SList functions, GetCurrentThread, DuplicateHandle, IsProcessorFeaturePresent, GetThreadTimes).
- `tools/tests/ifk_surfpoly_oracle.sh`: cases from tools/stl_case.py (every visible kind 0 surface of a part, at most
  60), wr_stl tolerance 0.001 and 0.01 inch (x 25.4 for metric parts), port vs DLL byte for byte (MESH + every Printf),
  then every distinct printed value through msvcr120.dll's sprintf and ifk_msvc_f.
- Results (2026-11-12, 533 corpus parts with visible kind 0 surfaces, 2 tolerances each): **1060 of 1066 runs
  byte-identical** (13836 surface runs, 3,541,688 facets, MESH and every Printf). The 6 others are the 3 copies of
  ROUGHPROJNCI (both tolerances): 20 of their 24 surfaces identical, the 4 degenerate ones refused by the port (12.4).
  %f: **2,678,379 distinct printed values, all the same text as msvcr120.dll's sprintf**.
- `tools/tests/ifk_surfpoly_test.sh` (no DLL needed): 5 synthetic records (tools/tests/ifk_surfpoly_cases.py: bilinear,
  the same with the normal reversed, a rational quarter cylinder, a non-uniform bicubic, a plane at z = -0.0) against
  the DLL's output kept in tools/tests/ifk_data/surfpoly_syn_dll.txt, and 1455 values against msvcr120.dll's %f text
  (ifk_data/surfpoly_f_msvcr120.txt: those printed plus k / 128, 1.005, 2.675, 156.8275, -0.0, 1e20, FLT_MAX ..).

### 12.7 NOT DECODED / not ported (state 2026-11-12)
- Solids: Parasolid's faceter (PK_TOPOL_render_facet) - third-party, not decodable here; selected solid faces:
  ps_face_to_mc_surf (Parasolid) and who sets convert_solids_to_surfaces.
- make_surf_polygons on kinds other than 0 and 10 (and kind 10 over them), on degenerate surfaces (stale stack values),
  and on trimmed surfaces the repair FUN_180586a50 / was_bad_trim_found and the other refusals of 12.8.
- Polygon meshes (copyPmeshToStlFile), binary STL. Not wired into the app yet (the commands stay greyed, N11-5).

### 12.8 Trimmed surfaces (kind 10 over a kind 0 base): the port [code; 2026-11-12]
Port: `viewer/mcpartline_trim.inc` (the loops as FUN_180591650 reads them), `mcpartline_trim2.inc` (the loops after the grid),
`mcpartline_trim3.inc` (the cells), included by `viewer/mcpartline_mesh.c`, whose `plm_mesh` takes kind 10 (the base from
`mcam_surf_base`; only a kind 0 base is ported). Every function cites its address; the arguments Ghidra loses (most of the
engine's) and the floating-point order are the disassembly's (objdump of mccore.dll). FUN_18057a980 on kind 10:
- FUN_180591650 (step x 0.1 = in_stack_90 as tol): the loops packed (the record's loop starts are not used, the counts are),
  each uv point clamped into the base's +0x70 range; FUN_180598070: per loop the closing repeat dropped, repeats and spikes
  removed on a circular list (FUN_180579660 at 1e-10: the middle point where the path doubles back), the loops packed again
  (a dropped loop still takes one slot), loops of fewer than 3 points dropped; FUN_180577e10 per loop (a, b, c, d: d == a,
  d on a-b, a on c-d (FUN_180592ce0) or a-b crossing c-d (FUN_18058a7c0, t along c-d, DAT_1812f4868 = 4e-14; its first
  index is the EDX FUN_180592ce0 leaves): when b, c and that point are within tol in 3D, b = c = the point and all of it
  again); then per loop FUN_1805776f0 (itself) and FUN_180578340 (the loops before): a crossing fails it. The DLL skips
  runs of segments outside the segment's box; the port compares every pair with the same tests (the skip is
  conservative). Then the outer loop (FUN_180586880: the smallest u), the per-loop boxes, an island with a point outside
  the outer loop (in_polygon -1) drops all islands, is_bdy_ccw per loop (negated for islands). The grid's box = the loops'
  boxes (18057ae8d).
- The grid as for kind 0 on the base with that box (FUN_180581f00, the counts, FUN_18058e2a0, FUN_18057e440,
  FUN_180599580), except: **FUN_18058e2a0's start is [R13 + 8] / [R13 + 0x18] with R13 = the entity's own surf_type**
  (18057ba25 / 18057bb85): for a trimmed surface its own +0x70 / +0x80 (its loops' box), not the base's range - it decides
  whether the last spans are merged (uniform spans only; invisible on kind 0).
- FUN_1805937e0 cuts the parameters to the box (1e-12); FUN_180577130 gives an island no grid line crosses one through its
  box middle along its longer side (moved in 2 % steps off the other loops' box ends, FUN_180599630 at 2e-10; not inserted
  within 2e-10 of a parameter); FUN_180576e90 walks each loop counter-clockwise (is_bdy_ccw 0: backwards), each point
  snapped to the parameters within 1e-10 (FUN_180598d40), adding every grid crossing on the way (FUN_18059bb90: the nearer
  of the u and v crossing, v on a tie; the crossed parameter exact, the other coordinate interpolated).
- With tol > 0, on a copy: FUN_18057f9a0 (loops of more than 3 points: FUN_1805812e0 makes a corner on one grid line whose
  neighbours lie in one interval a repeat; per run of points inside one cell (FUN_18058a980 / FUN_18058b700: the first
  point off the grid lines; the run grown both ways to the cell edge, across the loop's wrap) FUN_180581410 drops points
  while the 3D chord holds them within tol (FUN_180581710: dist_pt_l_3d and the uv midpoint; ends closer than tol in every
  coordinate always pass) - with several loops a run left wholly on the cell edges gets its first inner point back; a loop
  wholly in one cell goes to FUN_180580470 (the same simplification, then with fewer than 3 distinct points left a
  triangle: the longest step (z not compared: SUBSD XMM1,XMM1 at 1805809b1), a third point off the chord by the largest
  deviation or 1e-6 inside the cell, else a small triangle about the loop box's middle whose side choice takes the cell's
  u middle for both u and v (180580e2b)); then the loops packed, a 2-segment loop given a third point 2.00002e-05 of the
  chord off it (collinear) or 2e-10 steps (degenerate), the loop boxes grown); FUN_180598070 again (the loop count is kept
  even when it fails, the copy is not); the points snapped again, the islands' first points tested inside the outer loop,
  FUN_180577620 orients them (outer counter-clockwise, islands clockwise); only then the copy replaces the loops.
- Vertices: the grid points, then every loop point in table order; **init_surf_const has no kind 10 case**, so all are
  eval_surf's of the base (FUN_1805b08d0, `plm_eval`), not eval_surf_const as for kind 0. A loop point's vertex index is the
  grid count + its index in the point array.
- Per cell (u outer, v inner; corners (u0 v0) (u1 v0) (u1 v1) (u0 v1), indices b, b + nv + 1, b + nv + 2, b + 1):
  FUN_1805913b0: the outer loop wholly in the cell is the one piece; else FUN_1805909d0: each loop leaving the cell is marked
  (3 out, 2 in, 1 where it enters; FUN_180591130 decides at edge points whether the loop runs along the edge), a corner equal
  to a marked point takes its vertex index (FUN_180598730); the pieces are traced from an entry to its exit, then to the
  entry nearest counter-clockwise before the piece's first point (FUN_180592ae0 with FUN_180592eb0's edges 0 bottom .. 3
  left and FUN_180592a30), the corners between added (FUN_18059be40), closed by FUN_18059b950; with no piece and the cell's
  middle inside the loops the cell itself. The marks stay from cell to cell (the arena block local_2c70; the port starts
  them at 0, which the oracle agrees with); a cell holds at most loop points + 4 piece points (local_2d3c). FUN_180592030
  cuts each piece by ear clipping: the corner (prev, cur, next) when it turns left (cross > 0) or, once the strict passes
  stall, a straight one with cur between; FUN_180592f60 rejects an ear holding another live point (FUN_18058aea0, tolerances
  9.9e-11 / 9.801e-11); the triangle (prev, cur, next) as vertex indices. m->nv = the largest index used + 1.
- Refused (the port returns why; NOT PORTED / NOT DECODED): loops that still cross after FUN_180598070 (the repair
  FUN_180586a50, up to 30 tries, and was_bad_trim_found -> no triangles); a record without loops (FUN_180591650's
  edge_alloc(8) branch); FUN_180581410 reading point -1 (a run wholly on the cell edges with no inner point);
  FUN_180580470 taking the point after the loop's last; a non-strict ear test touching a point (FUN_180599690 /
  FUN_1805999f0); bases other than kind 0; degenerate bases (12.4). Not modelled: the arena sizes (make_surf_polygons'
  retry FUN_1806fb740 grows the arena until the run fits; the vertex block not fitting, local_2c88 = 0 with vertices per
  cell, is not ported) and the triangle capacity.
- Oracle: `KINDS=10 tools/tests/ifk_surfpoly_oracle.sh OUT LIST` (tools/stl_case.py --kinds 10: every visible kind 10 over a
  kind 0 base, at most 60 a part, its blanked base written too). 2026-11-12, the 423 corpus parts with such surfaces, wr_stl
  tolerance 0.001 and 0.01 inch: **822 of 846 runs byte-identical** (13032 surfaces, 2,527,786 facets, MESH and every
  Printf); in the 24 others 1266 surfaces identical and 36 refused - 30 needing the repair FUN_180586a50 (TUTORIAL 9 and
  CURVE 5 AXIS SURFACE EDGE ONE MULTIPLE SURFACES, all versions), 6 degenerate bases (ROUGHPROJNCI); **no surface
  different**. %f: 5.6 M values (distinct per run chunk), all msvcr120.dll's text. The loop stages can be compared alone:
  tools/peloader --stl with `STL_HOOKS` (call-site counters and dumps, tools/peloader/mccore_stl.c: 'L' the loops at
  FUN_1805913b0 = the port's `PLT_DUMP=1` "LOOPS 0", 'G' the grid parameters, 'A' / 'M' arrays, 'T' argument registers).
- Self-test: `tools/tests/ifk_surfpoly_test.sh` runs `ifk_surfpoly_cases.py --trim 0.02` (6 synthetic trimmed surfaces:
  notched rectangle, clockwise circle with an island, heptagon inside one cell (FUN_180580470), repeats / collinear points
  with the normal reversed, a square on grid lines, an island crossed by no grid line (FUN_180577130)) against the DLL's
  output kept in tools/tests/ifk_data/surfpoly_trim_dll.txt.
- Kind 0 rechecked after this port (KINDS=0, every part with visible kind 0 surfaces, 2 tolerances): 1268 of 1274 runs
  byte-identical (13836 surfaces, 3,541,688 facets: the same totals as 12.6), the 6 others ROUGHPROJNCI's degenerate
  surfaces as before (12.4).

## 5. Status
- 2026-11-12 (IF-1 / N12-1, 12): wr_stl -> surf_to_stl ported with MSVCR120's %f (ifk_surfpoly.c) on plm_mesh;
  DLL oracle tools/peloader --stl / --crtf: 1060 / 1066 corpus runs byte-identical (the rest: 4 degenerate surfaces,
  stale stack, NOT DECODED), %f identical on 2.68 M values. Next: trimmed surfaces (12.4 call tree), then wiring.
- 2026-11-12 (12.8): make_surf_polygons on trimmed surfaces (kind 10 over kind 0) ported (viewer/mcpartline_trim*.inc):
  KINDS=10 oracle 822 / 846 corpus runs byte-identical, 14298 surfaces identical, 36 refused (30 need the trim repair
  FUN_180586a50, 6 degenerate bases), none different; %f identical on 5.6 M more values. Next: the repair, the other
  kinds, wiring.

## 13. The DLL oracle: Mastercam's own Turn Profile / Silhouette chain under tools/peloader (N12-3, 2026-11-12)

### 13.1 What runs [code: tools/peloader/tpif.c, tpif_rt.c]
`peloader ~/mastercam-re/files/tpiftools.dll --tpif spinsil|silbndy IN.iff OUT TOL [--format F] [--metric M] [--box ..]
[--workdir DIR] [--vcdir DIR]`:
- pe.c gained a module registry: with `g_pe_autoload` every imported DLL found in the search path is mapped (recursively,
  dependencies first), imports resolve to the mapped images (by name or ordinal, forwarders followed), delay imports are
  resolved at load (OLEACC of the static MFC). Mapped: tpiftools, the 11 IF*.dll, the 20 interfacial*.dll and the real
  VC 2013 runtime `msvcr120.dll` / `msvcp120.dll` / `mfc120.dll` (12.0.21005) read from <windows>/Windows/System32
  (read only; env PELOADER_VC120 / --vcdir). So MFC, the C++ iostreams of the log, the CRT and libm are Microsoft's own.
  Everything else (KERNEL32 / USER32 / GDI32 / ADVAPI32 / SHLWAPI) is answered by win32.c / tpif_rt.c with plain OS
  behaviour (TLS, resources from the module's own resource tree, en-US locale, files, one 1920 x 1080 monitor for the
  static MFC's global data). seh.c walks frames over all mapped images: C++ exceptions thrown and caught across
  tpiftools / msvcr120 / the kernels work (seen: UIDlgComputeMC::ErrorMessage's throw, 13.4).
- All DllMain run (dependencies first), then the sequence of mckernel: ComputeMC::GenerateInterfacialSecurity()
  (mckernel's DllMain FUN_18012eed4 l.237380), UIDlgComputeMC(), SetWorkingDirectory(temp dir), SetLogFilePath(temp dir
  + "spinsil_log.txt" / "silhouette_log.txt", true), SetMessageLogging(true), SetMessageHandler, SetMessageSending(true),
  ComputeMCSpinsil(dlg, dir, in, out, tol, format, IsMetric) / ComputeMCSilhouette(dlg, dir, in, out, tol, IsMetric),
  EnableDataTransform(box_lo, box_hi), RunComputations (TPSpinSilhouette::Compute 1801252d8 / TPSilhouette::Compute
  180124b24). The flag is SystemContext+8 = IsMetric() (mccore MCSystemContext::MCSystemContext 180929710). The message
  handler (mccore's MCHSTMessageHandler: progress bar, user abort) is not given: SetMessageHandler ignores NULL and every use
  of UIDlgComputeMC+0x128 tests it (tpif_dialog.c), i.e. no progress display and no user abort.
- Licence (N12-3 terms): the vendor code makes the token (GenerateInterfacialSecurity) and the kernel checks it
  (IFSpinsil::Run FUN_180040990 -> interfacial08 Modulock::Run; i11 likewise). Nothing there is decoded, patched or
  emulated. Observed from outside (PELOADER_TRACE, import calls only): both the generator and the check call
  GetComputerNameA and ADVAPI32 GetCurrentHwProfileA, the check also reads the time. With a first stub that made
  GetCurrentHwProfileA fail (no Windows hardware profile on Linux) the kernel logged "Access denied."; answered as Windows
  does (success, an undocked profile, the GUID formed from /etc/machine-id, name "Profile 1"; tpif_rt.c) the vendor check
  passes on its own and the kernels run. GetComputerNameA = the host name, upper case, 15 characters.
- A hardware fault inside the DLLs is not turned into a Windows exception: tpif reports `signal N at module+RVA`, exit 4.
- Observation hooks (debugging only, the computation is unchanged): TPIF_TRACE_I11=1 wraps interfacial11
  PlaneCutToSilhouette slots 3 / 4 (AddPoint / AddEdge, vftable 18021beb8) and logs the PlaneCut output; TPIF_TRACE_RESOLVE=1
  hooks ifspinsil FUN_18003cd90 at entry (its 18-byte prologue is position independent) and logs each resolved edge with
  its quadtree groups; TPIF_TRACE_STITCH=1 wraps the EdgeSet's StitchIO slots 1 / 3 (vftable 18004b268: AddNode /
  AddTriangle, i.e. what Stitch emits; the EdgeSet's own axis / closest-approach nodes show up there too). Port side:
  IFK_TRACE_PC (ifk_silhouette.c), IFK_TRACE_RES and IFK_TRACE_ST (ifk_spinsil.c).
- Environment limits: HeapAlloc is calloc with a size header (zeroed memory); the order of pool blocks in memory is
  therefore glibc's, not the NT heap's (13.5). One run: about 0.2 s, 34 modules, ulimit -v 4000000 is enough.

### 13.2 Comparison (tools/tests/ifk_oracle_test.sh, tools/tests/ifk_oracle_cases.py, tools/tests/ifk_curve_cmp.py)
The port runs as ComputeMC does (`ifk_cli --mc`: box of the input nodes, ifk_mc_transform = getTransVec /
CheckDataLocation / CheckDataSize, the work copy rewritten, the kernel at tol x scale, the curve file, ReverseTransformation).
Cases: the self-test solids (cylinder, cone, stepped, tube, sphere; both kernels; a box for Silhouette), then random ones:
random profiles (pointed ends on the axis, steps), 3 .. 48 facets, random phase, sizes 0.001 .. 300 (extents beyond 3950
reach GetScaleFactor), translations up to 5000 (CheckDataLocation, also off the axis for Turn Profile), tolerances
1e-6 .. 0.1 (below 5e-5: checkToleranceValues scaling), IsMetric 0 / 1, Silhouette under random 3D rotations.
Per case: BYTES = identical curve files; FAULT both = the DLL faults and the port reports the same case; NONE both = no
file from either (Silhouette "IFSilhouette error 2: overflow." in both); GEOM / DIFF otherwise.
| set | Turn Profile | Silhouette | result |
|---|---|---|---|
| seed 1, 60 cases | 33 BYTES + 1 FAULT both | 24 BYTES + 2 NONE both | 60 / 60 |
| seed 7, 400 cases | 234 BYTES + 9 FAULT both | 153 BYTES + 4 NONE both | 400 / 400 |
| seed 11, 600 cases | 351 BYTES + 8 FAULT both | 231 BYTES + 10 NONE both | 600 / 600 (after 13.3 item 7; one Turn Profile DIFF before it) |
Before the fixes of 13.3 (first run, seed 1): 13 BYTES, 27 GEOM, 20 DIFF of 60 (cone / sphere 0 curves before the
walk-end fix).

### 13.3 Port fixes found with the oracle [code]
1. **Walk end** (FUN_18003e860 l.48504-48509, the only return): every output edge's ends are replaced by their union
   roots before Run's output loop. The cone / sphere apex is one node, their profile is written (was 0 curves, 9.4).
2. **Shell2D::EdgeQuadtree ported** (FUN_18003e370; Quadtree template FUN_180005820 traversal, FUN_180009180 split,
   FUN_180009400 smallest cell, FUN_18000d870 distribution, QTreeEdge FUN_1800051a0 clipping with FUN_180009060,
   FUN_180005c30, FUN_18003aca0 groups): FUN_18003cd90 and the seed FUN_18003d4c0 iterate the groups as the DLL does
   (replaces "boxes overlap", section 8). It decides which edge of a pair is E (crossing from E's line, union root =
   E's end: the axis zeros' signs) and the seed (output order).
3. **Pool addresses**: the MemoryPools fill a new block of 1000 through the free-list head (ifspinsil FUN_180012930 /
   FUN_18000e3d0, interfacial11 FUN_180027740), so a block hands out DECREASING addresses; a duplicate vertex / node is
   freed at once (FUN_180037a60, i11 FUN_180023710) and its record reused. EdgeSet edge keys and the first end of an
   edge (FUN_180037450, so the Shell2D edge direction) and the Silhouette edge tree (FUN_180023f80) now use these
   emulated addresses (vaddr / naddr). Sections 8 and 11.6 assumed increasing.
4. **Shell2D+0x68 mode**: IFSpinsil::Run sets +0x68 = 1 (FUN_180001280 right after FUN_18003dad0), which section 6 / 8
   missed. Each walk step is transactional: FUN_18003cd90 saves E's groups in E's record (+0x38) and FUN_18003a050 puts E
   (and in FUN_18003c5e0 F) on +0x80; FUN_18003c5e0 reuses a committed crossing of the same input-edge pair (map +0x70,
   FUN_1800399c0 key), FUN_18003a9e0 a committed sub-edge (map +0x78, FUN_1800398c0 key); after the choice
   FUN_18003a890 commits the chosen edge (crossing ends get a record, [3] = its input edge); FUN_18003a5e0 then undoes
   the step: the step's sub-edges leave their ends' lists (unvisited ones freed), the +0x80 edges drop their pending
   hits, uncommitted crossings are freed, split edges get their saved groups back (FUN_180004ee0) and rejoin their ends'
   lists. A step starts by resolving the item vertex's committed edge (record [3]) or the head of its list. The seed
   commits and rolls back too. Effect: a crossing is recomputed in later steps and the committed one may come from a
   pair test with E and F the other way round (last-ulp coordinates of off-axis parts).
5. **Fault**: a component without any edge (every EdgeSet edge interior or collapsed, e.g. a part smaller than the
   tolerance) has a NULL extreme vertex; FUN_18003c000 -> FUN_180005d50 -> FUN_18003bfa0 -> FUN_18003b890 reads
   address 0 (the oracle faults at ifspinsil.dll+0x3e9d, inside FUN_180003e80). The port returns IFK_SPIN_FAULT and
   writes nothing (no defined Mastercam result).
6. Silhouette translation: ComputeMC's and ComputeMCSilhouette's adjustTransVec are one empty function (export RVA 0x4630,
   `ret`), so Silhouette keeps all three components of t (ifk_mc_transform, spin = 0); Spinsil zeroes t.y, t.z.
7. Stitch T-junction fan (section 7 item 6): each piece is made by FUN_18000aea0, which frees it and returns NULL when two
   of its corners are the same node; FUN_18000aa60 emits a middle piece only when non-NULL and FUN_18000b1d0 neither
   splits nor emits a NULL first / last piece. The port emitted such degenerate pieces (seen on a part smaller than the
   tolerance, seed 11 c569).

### 13.4 Open items settled
- 9.4 (cone / sphere 0 curves): the DLL writes one apex node; fixed (13.3 item 1).
- 11.5 / 11.9.7 "what the dialog does after a message with argument 1": UIDlgComputeMC::ErrorMessage (180334820) sets a
  code (0x1e by default; "mem.." 0x1f, "o..o..m" 0x1f, "esc" 0xc9, "s...e.....3" 0x5a) and throws it as an int; the run
  ends, the log says "Process interupted", RunComputations returns 30 and no curve file is written. Seen with "IFSilhouette
  error 2: overflow." (4 + 2 cases); the port's stop is right.
- The Turn Profile tolerance check of the licence path and "Access denied.": passes in this environment (13.1).

### 13.5 Remaining differences / NOT DECODED
- Pool blocks beyond the first 1000 records: their relative addresses are the heap's (NT heap on Windows, glibc under
  peloader); the port assumes increasing. Seen once (seed 1 c059, 4146 vertices): the DLL ordered the edges at one vertex
  with the third block before the second; the output was the same. A part with ties decided across blocks may differ.
- Not covered by the cases: Turn Profile with format 1, axes other than X (mcmsurf's FUN_1802339f0 transform is outside
  the kernel), meshes from wr_stl (IF-1), more than 60000 output nodes.

## 14. The last Mastercam-side steps: dialogs, HBE -> chains, offset check, arc fitter, wiring (2026-11-12)

Sources: new Ghidra dumps of mccad.dll (project ghidra_cad), mastercam.dll arc_ep_3d / view_to_world / world_to_view
(project ghidra), systypes.dll arc_ep_3d (ghidra_systypes), mccadutil.dll BreakAtMidpoint (ghidra_cad); decomp/mcmsurf_all.c;
objdump of the DLLs [asm]; dialog resources of mccadres.dll (tools/pe_dialogs.py). Scratch dumps are not in git.
Oracle: `peloader mcmsurf.dll --mcms CASES [OUT]` (tools/peloader/mcmsurf_hbe.c, 14.7).

### 14.1 The commands and their dialogs (mccad.dll) [code]
Both are modeless MCDialog/CBCGPDialog panels run by an event loop (FUN_180072840 Turn Profile, FUN_180072540 Silhouette
Boundary): `get_event(.., 0x8200, ..)`; message 0x200 with 0xc9 = OK (run, close), 0xcb = Apply (run, keep the panel,
reselect), 0xca = Cancel; 0x8000 / 10 = the 3D construction toggle (dat[0x54]). Geometry: the panel's Geometry button
(0x9c5f) and the start of the loop call FUN_180073fd0 / FUN_1800722d0: `select_ents(prompt 0x9c58, mask 0x110100, ..)` with
allow_solid_surfaces = 1 (surfaces, solids and solid faces), the selection kept as sel bit 4.

**Turn Profile** (dialog 0xc14 "Turn Profile", class CTurnProfileDialog, ctor FUN_180072b40, DoDataExchange FUN_180072d90,
OnInitDialog FUN_180073560, message map 180413d50):
| control (mccadres 0xc14) | id | variable | default | dat field (FUN_180072f00) |
|---|---|---|---|---|
| Computation Method: Spin / Slice (radio, DDX_Radio Ordinal_3627) | 0x9c68 / 0x9c69 | DAT_1806a4554 (bss: 0 = Spin) | Spin | +0x4e = (method != 0); +0x4d = 2 + (method == 0) (3 Spin, 2 Slice) |
| Tessellation: Tolerance (CSkRCmb, recent values `saveRecentCombo(.., 0xc14)`) | 0x9c74 | DAT_18064b6b8 (inch) / DAT_18064b6b0 (metric) | 0.001 in / 0.02 mm | +0x10 |
| Profile: Upper / Lower / Full (radio) | 0x9c65 / 0x9c66 / 0x9c62 | DAT_1806a4558 (bss: 0) | Upper | +0x4c (0 upper, 1 lower, 2 full) |
| Axis (button, FUN_180073830 -> FUN_180073d10) | 0x9c77 | dialog +0x1558 / +0x1570, +0x1552 = 1 | none | +0x18 / +0x30 (below) |
Tessellation group / label / combo (0x9c75 / 0x9c76 / 0x9c74) are enabled only for Spin (FUN_180072e90). The variables are
statics of mccad (they keep their value for the session; no registry write in these functions). Other fields FUN_180072f00
sets: +0x00 = 0.001 in / 0.02 mm (fixed), +0x08 = -1.0, +0x48 = 100, +0x4f = 1, +0x50 = 1 (0 when +0x56), +0x51 = +0x52 =
+0x53 = 0, +0x54 = constr_view_is_3d (the panel's copy), +0x58 = the active origin (world), +0x70 = GetActiveViewNo(0),
+0x72 = ActiveOriginIsOn. Axis (+0x18 / +0x30, view coordinates): picked (+0x1552): the two world points through
world_to_view of the active view (Top: as is); not picked, origin on: o = world_to_view(origin) and o + (1, 0, 0) (Top: the
second through view_to_world, the identity there); origin off: (0, 0, 0), (1, 0, 0). FUN_180073d10: an event of mask 0x4003 / 0x400002:
a line (type 2 or 0x4000) gives its end nearer the pick (t >= 0.5: end first) and the other end; a point (1) asks for a second
point with a rubber band. +0x55 = 1 unless the axis is the X axis through the origin in Top (origin off, |p0.y|, |p0.z| < 1e-12,
view 1, |dx / len| >= 0.999999999999); +0x56 = |dz / len| >= 1e-6 (then +0x50 = 0).
Run (FUN_180072510 -> FUN_18006f480, inside UndoTransStart / UndoTransCommit or Terminate):
- Slice (+0x4e): FUN_18006f2a0: `sld_all_solid_surfs_to_temp_file(1)`, `slice_sel_surfs_with_a_plane(origin, plane x axis,
  0, 0, 1, 1, list, 0)` (mccore, NOT DECODED here), then FUN_18006ef50 and FUN_18006f540 (14.6). No Interfacial kernel.
- Spin with a selected solid (FUN_180070b20: get_ent(type 4, mask 0x100100)): FUN_1800709f0 (all selected faces of one solid
  -> select the solid), then FUN_1800704b0: CSpinBdryArcFitter(l_3d axis, tol = dat+0x10, false, 1, 0) = the Parasolid edge
  path (constructor 1800e0560, EvalPsBdry: out of scope, Parasolid). If it fails (fitter +0x191 = 0) the entities it made are
  deleted (get_raw_ent mask 0xc8e from the old db end) and the Interfacial path below runs with dat[0x4d] = 1, dat[0x53] = 0.
  On success: the fitter's entities (mirrored by FUN_1800703a0 when dat[0x4c]: mirror_silent about the axis in the profile
  view, copies when dat[0x4c] > 1) are the result.
- Spin without a solid: dat[0x4d] = 1, dat[0x53] = 0, FUN_180070060(dat, 1, list, 0) = mucbMakeIffSiluetBndy(dat, &chains,
  0, 0, false), the chains' entities collected (FUN_1800563f0), FUN_18006f6e0 and FUN_18006ef50 (14.6).
  So the Interfacial Turn Profile always runs with dat[0x4d] = 1, dat[0x4f] = 1, dat[0x53] = 0: the HBE converter takes the
  arc-fitter branch (10.5, 14.5) with filter tolerance dat+0x10 x 0.9 and lines only.

**Silhouette Boundary** (dialog 0xc16 "Silhouette Boundary", CSilhouetteBoundaryDialog, ctor FUN_180070dd0, DDX
FUN_180071150, OnInitDialog FUN_180071650, message map 180413560):
| control (mccadres 0xc16) | id | variable | default | dat field (FUN_1800712f0) |
|---|---|---|---|---|
| Tessellation: Tolerance | 0x9c74 | DAT_18064b6a8 / DAT_18064b6a0 | 0.001 in / 0.02 mm | +0x10 |
| Filter: Tolerance | 0x9c6e | DAT_18064b698 / DAT_18064b690 | 0.001 in / 0.02 mm | +0x00 (the HBE filter tolerance) |
| Filter: Look ahead | 0x9c6f | DAT_18064b688 (a double) | 100 | +0x48 = (int) value (the filter's maximum window) |
| Arc fit (check, DDX Ordinal_3547) | 0x9c63 | DAT_18064b678 | 1 | +0x50 (the filter's Top-plane arc flag) |
| Spline fit (check) | 0x9c64 | DAT_1806a4538 (bss) | 0 | +0x53 |
| Smooth Angle (degrees; enabled with Spline fit, FUN_1800712b0) | 0x9c72 | DAT_18064b680 | 5.0 | +0x08 = Spline fit ? angle x 0.01745329251994329 : -1.0 |
A changed combo value (|new - old| > tol) is added to the recent list (FUN_1800718c0 / 940 / 9c0 / a40). Other fields: +0x4c =
+0x4d = +0x4e = +0x4f = +0x51 = +0x52 = +0x55 = 0, +0x54 = constr_view_is_3d, +0x58 / +0x70 / +0x72 = origin / view / origin on
(FUN_180072150), +0x18 / +0x30 = world_to_view of the dialog's (origin, origin + x) (not used: no spin). Run (OK / Apply:
FUN_18006f140 -> FUN_18006fce0 inside UndoTransStart / Commit): mucbMakeIffSiluetBndy(dat, &chains, 0, 0, false); the
chains' entities collected; dat[0x4c] == 2 never holds here. So for Silhouette: filter tolerance = the dialog's, arc fit =
the check box (Top plane only), look ahead = the window size, spline fit = the angle break of 14.2 (otherwise every filtered
line record becomes a line entity).

Other callers of mucbMakeIffSiluetBndy in mccad: Interfacial_SilhouetteBndyChains (export, then FilterChains),
CreateSilhouetteBoundaryGeometryMS (export, dat[0x4d] 3 / 4 cases), FUN_18006ff60; not used by the two commands.

### 14.2 HBE file -> curves and chains: FUN_180232540 (ported: viewer/ifk/ifk_hbechain.c) [code, asm where marked]
Call (mucbMakeIffSiluetBndy): FUN_180232540(FILE, flat = (spin == 0 || !dat[0x55]), 0 (no scaling), 1.0, depth, &chains,
maxang = dat+8, 0 (no undo of its own)); depth = FUN_18021ef60(dat) (SpinBndyZDepth: CombineActivePlaneAndOrigin(.., 0 / 1,
1) origin z in view coordinates; dat[0x4d] 1 / 4: (false, true), 2: (true, true)) when dat[0x4f], else
mucbConstrDepthWcsOrigin() (= the construction depth, plus the active origin's view z when ActiveOriginIsOn).
14.2.1 The record loop: as 10.5, now exact. A point list (CMutlDlpDir, forward), duplicates refused against the node it is
added after ([asm] FUN_18021d2a0 / FUN_180239180: dy^2 + dx^2 + dz^2 < tol^2, mccore tol). A record with +0x61 and a non-empty
list: flush + reset first. Line: flat -> both z = depth; empty list: add the start (view_to_world unless view 1); the end
through view_to_world; count > 1 and !FUN_18020b970 -> flush, reset, re-add the old tail; add the end; count ==
get_max_n_spline_pts (4000) -> flush, reset, re-add the tail. FUN_18020b970: true below 2 points, **false for maxang < 0**
(so without Spline fit every line record ends a curve), else |acos(unit(tail - prev) . unit(p - tail))| <= maxang. Arc: flat
-> start, end, centre z = depth; count > 1 -> flush; count > 0 -> reset; mucbMakeMcArc3D(start, end, centre, normal z < 0)
on the record's own coordinates; then the arc end (view_to_world) starts the list. EOF: count > 1 -> flush. The success flag
is the value of the LAST flush / made arc (cVar2 is overwritten by every flush; a flush that makes nothing clears it).
14.2.2 Flush FUN_1802323a0: with more than 2 points every original segment is densified by FUN_18022fbd0 (depth 20): tangents
FUN_180230b60 (t0 = unit(p0 - prev) or unit(p1 - p0) at the first node, t1 = unit(next2 - p1) or unit(p1 - p0) at the last),
calc_chord_ht(p0, t0, p1, t1) <= curve_chord_dev: keep (the max is tracked, unused); else the midpoint ((p1 - p0) x 0.5 + p0)
is inserted after p0 and both halves recurse. Then the points in chunks of max_n_spline_pts (the next chunk starts with the
last point): mucbMakeMcSpline(n): n == 2 a line, n < 2 nothing, else make_spline_from_pts(pts, n, main colour, main level, 2,
unit(P1 - P0), 2, unit(Pn-1 - Pn-2), 0) = closed (|Pn-1 - P0| < tol: make_spline_coefs_closed, the tangents unused) or open
(make_spline_coefs_open with given end tangents), put_spline_in_db: class 8 parametric spline, its +0x68 / +0x80 = eval_spline
at t = 0 and t = nseg (not the nodes). A chord below tol: no entity.
14.2.3 Not defined: a midpoint within tol of its node returns CMutlDlpDir+0x48 unchanged (the last node added anywhere,
possibly freed by a reset) and the recursion continues on it; the port follows it while that node is live and reports
`undefined_hit` otherwise (never seen in the oracle).
14.2.4 mucbMakeMcArc3D: constr_arc_new(start.xy, end.xy, centre.xy, cw = normal z < 0, tol) (cw: the arc runs CCW from the
record's END to its start), arc_2d_to_3d(view = GetActiveViewNo(0), depth = centre z), arc_ep_3d (Mastercam.dll 18032d730 ->
systypes 180001210: c + r (cos, sin) of sa and sa + sweep through the view matrix), store_ent.
14.2.5 Entities: every one by store_ent(ent, &eptr, flags, main_color, main_level, main_attrib) - lines (class 2), arcs (4),
parametric splines (8, put_spline_in_db; store_ent_nodraw when main_color == gr_back_color); never NURBS. So the created
geometry gets the current main colour, main level and line style / width. The arc fitter's tp_ent colour byte
(lathe_cfg_stock_color) does not reach the entities (FUN_18020cb90 drops it).
14.2.6 Chaining (mucbAddCurveToChain 18022f730 with mccore add_curve_to_chain / set_chain_closed / chain_endpoint /
append_chain): the first curve opens a chain (RecordBoundaries_Add_Chain); a curve whose start / end meets the chain end
(nc_tol, 3D) is appended entered at that end; else one meeting the chain start is prepended (entered at its other end);
else appended anyway. set_chain_closed(nc_tol): start / end distance (2D in the active view when chain_ignore_depths, 0 in
the DLL image; the config's value at run time is NOT DECODED) < nc_tol; a closed chain moves to the output list and the
next curve opens a new one. Result: the closed chains in order, or the open chain when none closed; with closed chains an
open leftover is not returned but its entities stay in the part.
14.2.7 Port: `ifk_hbe_to_chains(records, n, params, out)` (viewer/ifk/ifk_hbechain.h): entities with the a_3d / spline data,
chains as (entity, entry end). Spline coefficients by viewer/mcspline_fit.c (mcsf_coefs_open / _closed). Top view (view 1);
other views use the view matrix rows given (view_to_world = R^T v, arc_ep_3d the same matrix) - not oracle-checked.
Self-test tools/tests/ifk_hbechain_test.sh; oracle tools/tests/ifk_hbechain_oracle.sh (14.7).

### 14.3 The offset check FUN_18021ff80 (Silhouette Boundary only) [code]
Runs when converter +0x12 (= spin == 0). FUN_18020c6d0: RecordBoundaries_Add_PockBnd(n records); FUN_18020c5a0: entity i =
{index i, 'L', 0x4d, start x, y, end x, y (z dropped), +0x78 = 0}, bound +0x48 = the 2D length; FUN_18020da60 per entity box;
bound +0x18 / +0x50 / +0x58 / +0x60 = 0; +0 = 1, +1 = +2 = +4 = 0, +8 = 0, +3 = (curve header +0x148 == 0) = the curve has no
parent tree node (11.9.4: the format 1 header's second int = parent id); **+0x20..+0x38 = converter +0xb0..+0xc8 = the curve's
2D box** (FUN_180213ee0 resets it with FUN_180242280 to (1e38, 1e38, -1e38, -1e38), FUN_18021f540 / FUN_18020dbc0 /
FUN_18020c1c0 grow it by every record end with FUN_180252970: min x, min y, max x, max y; this closes the 10.5 item
"conv+0xb0..0xc8"). Then MCCECalcOffset(1, -|converter +0xa0| = -|dat+0x10 x 10|, bound, &out, 0x100, 0.0, 1e-6, 1e-6); keep
the curve when out != NULL; both bounds freed. Dropped curves are neither filtered nor written.
Port: viewer/ifk/ifk_offchk.c builds this bound exactly (`ifk_offchk_bound`); **MCCECalcOffset's result is NOT DECODED** (the
offset core of mcce.dll, formats/mcce_engines.md 6.13; this project's MCCE port uses a Clipper / own-construction substitute
there) - `ifk_offchk` takes it as a caller predicate and returns IFK_OFFCHK_NOT_DECODED without one. Observed with the DLL
(peloader --mcms O cases, mccore's MCCECalcOffset -> mcce.dll): a 10 x w rectangle at |d| = 0.01: kept for w = 0.0201 and
0.025, dropped for w = 0.0199 and 0.02 (i.e. kept iff wider than 2|d|), when its orientation agrees with byte +3 (CCW with
no parent, CW with a parent); the opposite orientation is dropped; an open polyline is dropped; d = 0 keeps everything.
[observed, 12 cases: not a decode]. Question for the user: IF-2 in formats/TODO.md.

### 14.4 The HBE filter checked against the DLL
`peloader mcmsurf.dll --mcms` F cases call mcmsurf's own FUN_1802097b0 (args [asm 18020dea5..18020deef]: this, +0x178,
+0x179, +0x17a, (short) +0x17c, mccore tol, +0x180) / FUN_18020dd40 / FUN_18020a020 on random curves
(tools/tests/ifk_hbefilter_cases.py: sampled arcs, straight runs, corners, open / closed, planes z, filter tolerances 1e-6 ..
0.018, windows 5 .. 100); tools/tests/ifk_hbefilter_oracle.sh: seeds 2 and 3, **1000 / 1000 cases identical** (66 k line and 7.4 k
arc records). viewer/ifk/ifk_hbefilter.c needed no change.

### 14.5 Turn Profile's arc fitter: CSpinBdryArcFitter (mcmachinedef.dll; decoded, ported and checked against the DLL)
Around it (mcmsurf, ported in viewer/ifk/ifk_tpconv.c, oracle 14.7): FUN_18020c830(conv, &n, 1): AllocBdryEntMemNormal(n + 1)
zeroed; running start Q = record 0's start; per record: tp +4 = lathe_cfg_stock_color, +6 = 3 (written before any test); a
non-line record: debug_str("... Arcs not supported at this time!"), skipped; a gap (|start - previous end| > tol, y-x-z order)
restarts Q; |end - Q| <= tol: skipped (Q kept); else tp: +0 = 2, +0x10 = Q, +0x28 = end, Q = end. FUN_18020cb90(conv, tp, &n,
1): fails when n > the filter's count; passes by dat[0x4c] (0: as is, 1: mirrored, 2: both; mirrored = y of start, end (and
for arcs centre y and the two doubles +0x60 / +0x68) negated); records: line type 1 (+0 / +0x18 from tp +0x10 / +0x28); arc
type 2, centre = tp +0x40, normal = (0, 0, tp +0x68 <= 0 ? -1 : 1); +0x61 = 1 on each pass's first record.
The fitter itself (constructor 1800e06f0):
Port: viewer/ifk/ifk_arcfit.c / .h (`ifk_arcfit`), driver viewer/ifk/ifk_arcfit_test.c (`make ifk_arcfit_test`); self-test
tools/tests/ifk_arcfit_test.sh; oracle tools/peloader/mdef_arcfit.c (`peloader mcmachinedef.dll --arcfit CASES [--out R] [--port]`),
tools/tests/ifk_arcfit_oracle.sh, ifk_arcfit_cases.py, ifk_arcfit_cmp.py. Decompiles: Ghidra projects mdef (mcmachinedef),
mccore, systypes; disassembly objdump. The class lives in FindArcs.cpp, its geometry in BEPoint / BELine / BEArc.cpp
(C3dPoint / C3dLine / C3dArc, all inside mcmachinedef), the boundary library (BoundaryMath.cpp etc.) too; mccore.dll and
systypes.dll give the 2D helpers, Mastercam.dll the planes and views.

#### 14.5.1 Call, object, result [code]
- mcmsurf FUN_18020de20 (10.5): `CSpinBdryArcFitter fit(&tp, cnt, axis, tol, dat[0x4d] == 4)` = 1800e06f0 (tp_ent **, short,
  l_3d &, double, bool); ok = fit+0x191; FUN_18020cb90(conv, tp, fit+0xa8, 1) reads the result with the count +0xa8 [asm:
  local_2c0 = fit+0xa8]; ~CSpinBdryArcFitter 1800e08d0 frees *tp (FreeBdryEntMem).
- Constructor: CBdryEntList base (1, 7, 7, 1); +0xa0 = tp_ent **, +0xa8 = n, +0xaa = 0 (mirrored), +0xd8 = 0 (profile plane
  id), +0xe0 vector<C3dArc> (0x78), +0xf8 vector<C3dLine> (true lines, 0x40), +0x118 C3dLine axis, +0x158 = tol (<= 0: mccore
  `tol`), +0x160 vector<C3dLine> (the IF lines), +0x178 / +0x180 = main_color / main_attrib saved, +0x190 = flag;
  +0x191 = DefineNamedView() && (EvalIFBdry(true) || EvalIFBdry(false)). The other constructor 1800e0560 (l_3d, tol, bool,
  bool, char) runs EvalPsBdry (Parasolid SpinSolidSilhouette, mccad FUN_1800704b0) - out of scope.
- Tolerances: g_sysTol 180369030 = 1e-8 (static initialiser 1800030d0), g_angTol 180369050 = 1e-6 (180003070), g_distTol
  180369040 = |mccore tol| when mcmachinedef is loaded (1800030a0; [inferred] 5e-5 = mccore's static value 1812b5b58, the DLL
  being loaded before the configuration sets tol). C3dPoint::operator== (18002c4c0) is |b - a| < g_distTol (sum dy2 + dx2 +
  dz2); mccore `tol` is read directly (axis length, UseNamedView, the boundary library) and by every mccore / systypes
  helper; mccore min_arc_length (static 0.0002) by BreakBdryArcQuadrants and FUN_18003db80.

#### 14.5.2 DefineNamedView 1800e1fb0, UseNamedView 1800e5bb0 - the profile plane and its side effects [code + asm]
- Axis length < mccore tol: false (the fit fails). UseNamedView(plane): the line from the plane's origin
  (GetOriginInWorldCoordinates) to origin + X (Matrix33::GetX of GetMatrix33) passes through both axis points within mccore
  tol (C3dLine::PassesThru 18002b560, unbounded: proj_pt_ln_3d, distance <= tol).
- Order: flag 0: the active WCS (GetActivePlanePtr(3)) then the active C-plane (0); flag 1 the other way round; a match sets
  +0xd8 = GetActivePlaneID(that type). GetPlanes is always called; without a match the planes are tried in GetPlanes' order.
- No match at all [asm 1800e215d..1800e2449]: rows v = unit(axis end - start), w = v through world_to_view(C-plane view),
  z = 0, then (|w| >= tol) rotated by pi/2 about the origin (C3dPoint::RotateXY) and back through view_to_world, else the
  C-plane's X; normalised; n = unit(v x w); origin = axis start. SearchPlanePtr(Matrix33(v, w, n), P3D(origin)); when it finds
  nothing: `Plane(M, origin, 0)` and **MastercamPlanes::Add(plane, false, true)** (a new named plane in the part), +0xd8 =
  its id.
- **SetActivePlane(0 = C-plane, ProfileView)** whenever the plane did not come from the active WCS / C-plane (GetPlanes loop
  or new plane). Nothing else in the part changes on this path: DumpToDb (180024800) is empty, no entity is written, colour /
  level are untouched (main_color / main_attrib are changed and restored only by the solid path, CreateEdgeCurves 1800e1e30 /
  DeleteEdgeCurves 1800e2540).
- ProfileView 1800e51c0 = GetPlanePtrByID(+0xd8) (assert + the active C-plane when missing); mcmsurf world_to_views the axis
  with its GetShortViewNo(true). The fit itself does not depend on the plane (no-solid path).

#### 14.5.3 EvalIFBdry 1800e25a0 [code]
- DeleteAll; n < 3 or no array: false. +0x160 = a C3dLine per tp_ent (+0x10 l_3d, whatever the type); +0xaa = 1 when an end
  has y < 0 (used only by AddLine/AddArcToList, solid path).
- Pass 1 (true): FindSpinBdrySolidTrueLinesAndArcs 1800e3260 clears +0xf8 / +0xe0 and walks get_ent(cursor, ..., 5, 0x10000)
  from db_start: every solid of the part (not only the selected ones) goes through ProcessSolid (its wireframe dependencies,
  its Parasolid edges converted to temporary entities, ValidateTrueLines against the IF boundary) - **NOT DECODED / not
  ported (Parasolid)**; without solids the lists stay empty. EvalIFLinesAndTrueArcs 1800e2820 then only removes the lines
  whose ends are equal (C3dPoint ==): RemoveIFLinesOnLine / -OnArc and ExtendDeadEndLineToArc need true lines / arcs.
- FitArcsToIFLines; FindOuterBdry; ToIFBdry when it succeeds, else _MCAssert (FindArcs.cpp 0x5d4) and false.
- Pass 2 (false, only when pass 1 failed) skips both solid steps; the arcs of pass 1 stay in +0xe0 (never cleared there).

#### 14.5.4 FitArcsToIFLines 1800e3540 [asm]
- Fewer than 4 lines: nothing. tolA = min(max(tol, g_distTol), 0.001). From line i (0 ..): only when
  Next3LinesAreAttached(i) (1800e48d0: >= 4 lines, i <= n-3, end(i) == start(i+1), end(i+1) == start(i+2)): B = perpendicular
  bisector of line i (PerpendicularBisector(false) 18002b600: the line rotated by +pi/2 about its midpoint); for k = i+1 ..:
  C3dLine::LineIntersect(B, bisector of k) must give 7 (a single point: ll_ints_3d with both closest points equal, after the
  zero-length / equal / parallel tests of 18002acc0); centre c = that point for k = i+1, else the mean of the points so far
  (AvgArcCenter 1800e1540, z = 0), radius R = |c - start(i)| or the mean of the distances so far (AvgArcRadius 1800e16a0);
  d = |end(k) - c|; stop when |R - d| > tolA, the length ratio of k to the previous line > 2 (either way), or |d - |mid(k) -
  c|| > 1.1 tol, or C3dArc::FromXYPoints(start(i), end(k), c, cw = z of (start(i) - c) x (end(k) - c) < 0) fails, or its
  |sweep| > pi; else the point and d are kept.
- When at least lines i, i+1 passed: FitArcThroughPoints 1800e33d0 (points start(i), end(i), .., end(k-1)) ->
  C3dArc::FitToPoints = FitToClosestCenter 180026d10: over all chord pairs the bisector intersection with the smallest
  max - min of the distances to all points (strict <, from 1e30); the arc from the first to the last point about it
  (FromXYPoints, cw from (p0 - c) x (p[n-2] - c)), else From2PointsAndRadius 180027390 with |c - p0|. Success: SetView(1),
  appended to +0xe0, lines i .. k-1 erased, i stays; failure: i = k. No arc: i + 1.
- C3dArc::FromXYPoints 180027650 = mccore constr_arc_new(s, e, c, cw, &g_distTol) then Init 180027730 (r kept when >
  g_distTol, start angle and sweep through FUN_1800271d0 (snaps to 0, +-pi/2, +-pi, +-3pi/2 within g_angTol; the sweep with
  the -2pi lower bound and 2pi kept), end points from systypes arc_endpoints at the centre's z).

#### 14.5.5 FindOuterBdry 1800e31d0 -> FindOuterBoundary 180157a00 = FUN_180156d50(1, 1, ..) [code + asm]
- LinesAndArcsTo_tp_ents 1800e4630: lines (type 2), then arcs (type 4, a_3d, +0x70 = 1), through QuadrantBdryArcs 1801586f0.
- FUN_180156d50: z of ends / centres to 0, view 1 (FUN_180158b70); QuadrantBdryArcs again (BreakBdryArcQuadrants 1801547f0:
  break_arc_3d at 1.570796326794896 steps, points within tol of the centre's x / y snapped, pieces through PtsTo2DArc, shorter
  than tol dropped, below min_arc_length as lines, start angles / pi/2 sweeps snapped); BrkBdryEntsInts 1800410e0 (every
  entity against the others with an overlapping box + 2 tol: IntersectBdryLineBdry 1800457a0 / IntersectBdryArcBdry 180045190
  with ln_ln_ints2 180159bf0 (+ FUN_18015a420), line_arc_ints 180159630, arc_arc_ints 180158bc0; overlaps remove or trim
  (FUN_180047660, FUN_180047350); crossings sorted by t (FUN_18003e790, at most 100), the entity split (FUN_18003e3d0) and
  appended through FUN_18003db80 (drops degenerate pieces, full circles and duplicates; output at most 10000 entries));
  FUN_1801563a0 removes dangling chains; the start FUN_180156620 (side 1): the entity whose higher end (larger y, equal
  within tol: larger x) is topmost, ties by the walk rule from a virtual line (0,0) -> (0,-1); the walk: candidates with an
  unused end at the current end (FUN_180157090), FUN_180157170 takes the one turning most to the left (angle from the
  reversed end direction, [0, 2pi), ties within 1e-6: arc rules, two lines: tol / 10 and again, up to 10 rounds; candidates
  whose end meets a used entity are skipped; FUN_180153ea0 for equal directions), FUN_180153d00 merges collinear lines /
  arcs of one circle; until back at the first start (no candidate: failure); AdjustBdryEndPts 18003edf0; the array replaced.
- Success: the list = From_tp_ents 180024820 (lines copied, arcs rebuilt by C3dArc(a_3d) + operator= + copy, i.e. Init:
  end points recomputed); failure: FromLinesAndArcs (unused).

#### 14.5.6 ToIFBdry 1800e5980 - the output [code]
- More entries than +0xa8: the array is freed and a new one allocated and zeroed (AllocBdryEntMemNormal, *tp updated);
  +0xa8 = the count. Entry k: +0 = 2 and +0x10..+0x3f = l_3d, or +0 = 4 and +0x10..+0x77 = a_3d (start, end, centre, r,
  start angle in [0, 2pi], sweep (sign = direction, used by FUN_18020cb90), view 1, 6 zero bytes). **Untouched**: +4..+0xf
  (FUN_18020c830's colour / 3 stay on entries written over input entries), +0x40..+0x77 of lines written over input lines,
  and the entries beyond the count when the result is shorter. The boundary starts at the start entity of 14.5.5 (observed:
  the profiles run clockwise, e.g. from the top-right corner down the right end, along the axis, up the left end); mirrored
  halves (y < 0) take part as given.

#### 14.5.7 The oracle (tools/peloader --arcfit, mdef_arcfit.c) [code]
mcmachinedef.dll, mccore.dll, systypes.dll and the real msvcr120 / msvcp120 / mfc120 (System32, read only) are mapped (pe.c
autoload); only the three runtime DLLs run DllMain; mcmachinedef's statics the path needs are set as their initialisers do
(g_sysTol / g_distTol / g_angTol, the C3dPoint constants 18034bfa8 = (1e30, 1e30, 1e30) and 18034bfd0, the resource handle
180368760). Answered there: Mastercam.dll's planes (a plane set per case; GetPlanes in the given order; SearchPlanePtr finds
nothing and Add gives max id + 1, both logged), world_to_view / view_to_world (identity for views < 2, larger views logged),
Mastercam.dll arc_ep_3d (systypes arc_ep_3d with the identity for view 1 [inferred]), get_ent (no solid), _MCAssert (logged).
`--port` links the port into peloader and gives it msvcr120.dll's sin / cos / atan2 / acos (ifk_arcfit_set_libm).

| seeds (tools/tests/ifk_arcfit_oracle.sh) | cases | port with msvcr120's libm | port native (glibc) |
|---|---|---|---|
| 1, 7, 11 (600 each) | 1800 | 1744 BYTES + 40 NEW plane + 5 FAIL both + 11 REFUSED, 0 DIFF | 1663 BYTES + 39 NEW plane + 82 ULP + 5 FAIL both + 11 REFUSED, 0 DIFF |
| 21-24 (500 each) | 2000 | 1930 BYTES + 47 NEW plane + 7 FAIL both + 16 REFUSED, 0 DIFF | 1831 BYTES + 44 NEW plane + 102 ULP + 7 FAIL both + 16 REFUSED, 0 DIFF |
(2091 of the 3800 results contain arcs, 4251 arcs in all; REFUSED = a C-plane view >= 2 on the new-plane path.) The last-ulp
differences of the native build are all sin / cos (e.g. msvcr120 sin(0.8138806869715102) is glibc's + 1 ulp): with
msvcr120's functions every case is byte-identical. [inferred] msvcr120 picks an FMA3 code path for its x64 math on CPUs that
have it, so Mastercam's own last bits may depend on the machine.

#### 14.5.8 NOT DECODED / not ported
- The solid path of FindSpinBdrySolidTrueLinesAndArcs (any solid in the part; Parasolid edges, ValidateTrueLines with
  BOUNDARYCLASS) and the Parasolid constructor 1800e0560: the port assumes a part without solids.
- Which plane SearchPlanePtr returns, and the id / view number Add gives a new plane (Mastercam.dll): the port reports the
  plane (new_plane, matrix, origin) with prof_id 0 / prof_view -1.
- mcViews: world_to_view / view_to_world / Mastercam.dll arc_ep_3d for views >= 2 (the port refuses: IFK_AF_NOT_DECODED);
  view 1's matrix in arc_ep_3d taken as the identity [inferred].
- C3dLine::SaveColinearIntPts 18002b980 (colinear bisectors): only "not 7" is used, the codes are not ported.
- Limits: BrkBdryEntsInts' 10000-entry output (the DLL writes past it; the port returns IFK_AF_LIMIT), FUN_1801563a0's
  200-entry lists (the port has no limit).
- Paths the random cases did not reach (ported from the decompile / disassembly, unverified): FUN_180153ea0 (equal leaving
  directions), FUN_180047350 (two arcs of one circle overlapping), AdjustArcCenter retries in PtsTo2DArc,
  From2PointsAndRadius, the collinear-disjoint branch of FUN_18015a420.

### 14.6 Turn Profile's clean-up in mccad (after the chains) [code]
- FUN_18006f6e0(list, axis): both axis points view_to_world (active view); every line (type 2) whose two ends are collinear
  with the axis (FUN_180070470 -> FUN_18006ee50: the three components of (A - P) x (A - B) below tol, A / B the axis points,
  unnormalised) is deleted (delete_ent) and removed from the list. If none was deleted the axis is put through view_to_world
  a second time and the test repeated [sic].
- FUN_18006ef50(list, dat[0x4c], flag 1): s = -1 for the lower profile (1), else +1 (0x18040ae40 / 0x18040a698); for every line /
  arc / spline / NURBS: FUN_180070200(end, s, flag) = (v.y s >= 0 or |v.y s| < |tol|), v = world_to_view(end - active plane
  origin, the plane's view); ends on different sides -> BreakAtMidpoint (mccadutil 18004b960: length_ent, the half-length
  parameter, the break helpers FUN_18004c400 / FUN_18004bca0 / FUN_18004c9c0 in mode 2) and the new piece inserted after it.
  The helpers are NOT DECODED here (Modify > Break Two Pieces' port in cui_geo.c FUN_18004f9f0 may share them: unchecked).
- Slice only: FUN_18006f540 deletes entities with an end on the wrong side (s as above).
The commands then just leave the entities (the CArray) in the part; FUN_18006f480 returns whether any exists.

### 14.7 The oracle `peloader mcmsurf.dll --mcms CASES [OUT]` (tools/peloader/mcmsurf_hbe.c)
mcmsurf.dll with mccore.dll, systypes.dll, mcce.dll (its DllMain), no DllMain of mccore / mcmsurf, C runtime / MFC of mcce_rt.c
(glibc libm on both sides). Mastercam.dll is not mapped: GetActiveViewNo = 1, view_to_world / world_to_view = identity (Top),
arc_ep_3d = systypes' with the identity, curve_chord_dev / main_color / gr_back_color = harness variables; AllocBdryEntMemNormal
= calloc (mcmachinedef not mapped). store_ent / store_ent_nodraw / get_ent_from_eptr answered by an entity store,
write_ent_sel / _MCAssert / post_warning / debug_str no-ops. Case kinds: C (FUN_180232540 on an HBE file), O (FUN_18021ff80),
F (the filter), T (FUN_18020c830 + FUN_18020cb90), U (FUN_18020cb90 on tp_ent images). Results:
| test | cases | result |
|---|---|---|
| ifk_hbechain_oracle.sh seeds 1 / 2 / 3 (40 + 400 + 400) | 840 | 840 same structure: 592 byte-identical, 248 differ only in spline coefficients (max 1e-9 relative; mcspline_fit.c's make_spline_coefs rounding, see spline_fit_oracle.sh) |
| ifk_hbefilter_oracle.sh seeds 2 / 3 | 1000 | 1000 identical |
| ifk_tpconv_oracle.sh seeds 1 / 2 | 800 | 800 identical |
| ifk_arcfit_oracle.sh (peloader mcmachinedef.dll --arcfit) seeds 1, 7, 11, 21-24 | 3800 | 0 DIFF; byte-identical with msvcr120's libm (14.5.7); 184 last-ulp with glibc's |

### 14.8 What wiring the commands needs (not done: commands stay greyed)
1. UI (viewer/core_ui, cui_geo.c `geo_unsupported`): two panels with the fields of 14.1 (defaults, inch / metric, session
   statics, recent values), Turn Profile's Axis pick (line: nearer end first; or two points) and Method / Profile radios;
   selection mask 0x110100 (surfaces, solids, solid faces); OK / Apply / Cancel; one undo step per run.
2. Mesh: wr_stl (ifk_surfpoly.c: untrimmed NURBS only; trimmed surfaces, other kinds, solids and solid faces refused, 12);
   view not Top: ifk_transform_stl_view; the box ifk_stl_box; the binary IFF (ConvertStlFileToBinIFF, 3c).
3. Turn Profile: a selected solid takes the Parasolid fitter path first (out of scope: refuse, or go straight to the
   Interfacial path only with the user's approval); Slice (slice_sel_surfs_with_a_plane) NOT PORTED; dat[0x55]:
   ifk_axis_transform_surface (fill = false from these commands); kernel ifk_spinsil_run at tol x 0.5 via ifk_mc_transform
   (format 0).  Silhouette: ifk_silhouette_run at tol x 0.5 (format 1).
4. Converter per curve: ifk_hbe_records (scale 1, spin) -> Silhouette: ifk_offchk (needs the IF-2 answer) -> ifk_hbefilter
   `hbe_filter(plane_z = dat[0x50] (Turn Profile: 0), window = dat+0x48, filter tol = dat+0 (Turn Profile: dat+0x10 x 0.9), tol)`
   -> Turn Profile: ifk_hbe_to_tpents -> `ifk_arcfit` (14.5; the part's WCS / C-plane / plane list in IfkAfEnv; its side
   effects to apply: a new named plane (MastercamPlanes::Add) and the active C-plane set to the profile plane) -> on success the axis points rewritten in the profile view and
   ifk_tpents_to_hbe(profile = dat[0x4c]); the fitter failing leaves the converter's result false (FUN_18020de20 returns the
   fitter's flag: the curve's unfiltered records are written instead, FUN_18021f730).
5. dat[0x55]: ifk_axis_back_hbe; then ifk_hbe_to_chains (flat, depth of 14.2, maxang, tol / nc_tol / curve_chord_dev from the
   part's config, chain_ignore_depths from the chaining config).
6. Entities into the part: lines / arcs / class 8 splines with main colour, main level, main attributes (mcam_new_curve; the
   spline record via mcsf_spline_rec with p0 / p1 = the evaluated ends of 14.2.2, not the nodes); Turn Profile clean-up of
   14.6 (axis lines; BreakAtMidpoint NOT DECODED); repaint.
7. Tests: the four oracles above plus end-to-end cases once wired (STL -> kernel -> entities), against Mastercam's own files
   where a part with Turn Profile / Silhouette results exists.

### 14.9 NOT DECODED / open (this section)
- MCCECalcOffset's result for the offset check (14.3; question IF-2).
- CSpinBdryArcFitter: the solid path, SearchPlanePtr / Add's plane id and view, views >= 2, the limits and unreached
  paths of 14.5.8.
- The Parasolid fitter path (constructor 1800e0560, EvalPsBdry) and Slice (slice_sel_surfs_with_a_plane): out of scope / not
  ported.
- BreakAtMidpoint's helpers (14.6); where the run-time chain_ignore_depths comes from (14.2.6).
- FUN_180232540's stale-node path (14.2.3); view_to_world / arc ends in views other than Top are ported but not oracle-checked.
- make_spline_coefs_open / _closed are not bit-identical in mcspline_fit.c (coefficients within 1e-9 relative).

## 5. Status
- 2026-11-12 (N13, section 14): dialogs decoded; HBE -> chains, the filter, the tp_ent conversions and CSpinBdryArcFitter
  ported and checked against the DLLs (peloader --mcms / --arcfit, 0 DIFF); the offset check's MCCECalcOffset result open (IF-2);
  the wiring list is 14.8.
- 2026-11-12 (N12-3 oracle): tools/peloader --tpif runs Mastercam's chain; with the fixes of 13.3 both ports give
  byte-identical curve files on all oracle cases (13.2).
- 2026-11-12 (Silhouette stage C): the post-PlaneCut Stitch2D loop, FUN_180025000, FUN_180024da0 and Surfer's binary
  tree curve writer decoded and ported; `ifk_silhouette_run` and `ifk_cli --silhouette` run the whole Silhouette
  Boundary kernel (11.9); the written file is read by ifk_read_bin_curves(format 1) unchanged. Next: the oracle
  (N11-5), wiring into the app (mcmsurf side, 10), Surfer::ReadFile's call order.
- 2026-11-11 (PlaneCut stage B): PlaneCut ported to the curve tree for Silhouette's case (11.8: FUN_180029630 with
  the decoded angle-sort rule, test points, the PlaneCutToSilhouette classifier with the interfacial11 Shell2D side
  path, Polygon2D assembly / nesting, slots 0-5; `ifk_pc_run`). Next (stage C): Stitch2D, FUN_180025000, the writer,
  `ifk_silhouette_run` (decomp/interfacial/notes_planecut_stageB.md).
- 2026-11-11 (PlaneCut stage A): PlaneCut ported up to the split arrangement (11.7: exact layer, cut, points, pair
  intersections, splits, piece flags; tools/tests/ifk_planecut_test.sh). Next (stage B): FUN_180029630 onwards
  (pruning, angle sort, chains, test points, Polygon2D, the Silhouette classifier).
- 2026-11-11 (O3, branch of toolpaths2): curve file writer (9), Mastercam side partly (10), Silhouette decoded in
  outline with its input stage ported (11). Next: PlaneCut's exact layer (FUN_18001bde0), CSpinBdryArcFitter,
  make_surf_polygons (IF-1), the oracle.
- 2026-11-11: module map; Ghidra import + analysis of all 31 DLLs (project ifk); full dumps of the Turn Profile and
  Silhouette paths; call path 3 decoded down to the kernel entry. Same day: indirect-call sizes (4), the Mastercam-side
  pipeline (3b), both binary file formats (3c), the Turn Profile kernel (6: EdgeSet, Shell2D incl. its exact layer and
  walk) and Stitch (7). Turn Profile is decoded far enough to port. The port (8) passes its self-tests; the oracle is deferred (N11-5). Next: IFSpinsil::Run's stages (Turn Profile first), the curve
  header strings from the kernel writer, then Silhouette::Run.
