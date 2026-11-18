# Robustness review (2026-10-15) - status

> **mcamui removed 2026-10-22.** The nuklear app (viewer/mcamui.c, its `*_ui.inc` files, viewer/mcamgl.c and the
> Nuklear library) was deleted in phase 9 of formats/qt_port_plan.md; references to them below are historical. The UI is
> the Qt app (viewer-qt/src), the shared behaviour is in viewer/core_ui/ (optree_text.inc stays: core_ui cui_optree.c
> includes it). The deleted sources: ~/mastercam-re/backups/pre_nuklear_removal_2026-10-22.tgz.

Review of viewer/*.c for memory safety on untrusted input, fragile logic, error handling and G-code safety
(full findings below the status table). Checks after the fixes: fileop regression 189/189 identical, LinuxCNC post of
the 16 user parts byte-identical to before, all 16 user parts open in the GUI.

| # | Finding | Status |
|---|---|---|
| C1 | same-tool op change: diagonal rapid down to the feed plane | FIXED - rapid to the op start (clearance) first, as Mastercam's post ("X Y Z25." / "Z10."); verified on BRACKET4 |
| C2 | drill retract to clearance dropped after G99 | FIXED - modal Z = R plane after each hole |
| C3 | fmtn() stack overflow, NaN / huge coordinates reach G-code | FIXED - bounded copy; the post refuses non-finite / > 1e6 coordinates, feeds, R planes |
| C4 | operations silently left out of the program | FIXED - the post fails naming the ops; the message reports the count actually posted |
| C5 | project chains by entity index | FIXED - entity geometry stored with the index, re-found by geometry; an op whose geometry is gone is not regenerated (reported) |
| C6 | deleting geometry used by an op | FIXED - delete refused naming the ops (deviation from Mastercam, which lets it go); modify warns |
| C7 | Save as MCAM heap overflow on short records | FIXED - only full OP_ID records reused; header must be >= 0x80 bytes |
| C8 | library tool copied into a part of other units | FIXED - refused (Mastercam's conversion NOT DECODED) |
| C9 | Solid Chain edges always mm | FIXED - scaled to the part's units (metric 1, inch 1/25.4, unknown: extent match as the display); no inch solid part to test |
| H1 | edited ops move to the end of the program | FIXED - ops rebuilt / taken over keep their tree position (mcam_move_op) |
| H2 | post re-reads a part that changed on disk | FIXED - size / mtime checked, post refused |
| H3 | S0 / feed 0 reach G-code | FIXED - refused (negative rpm = M4 is Mastercam's CCW and stays) |
| H4 | unknown units posted as G21 | FIXED - refused for a loaded part with no stored toolpath |
| H5 | XT parser reads past the buffer | FIXED - every length bounds-checked |
| H6 | XT loop walks can hang | FIXED - guards on loop lists and angle unwraps |
| H7 | failed post deletes the previous program | FIXED - written to .tmp, replaces the old one only when complete |
| H8 | Windows: saving over a file fails | FIXED - file_replace (MoveFileEx) |
| M1 | NCI texts unterminated | FIXED - copy leaves the last byte 0 |
| M2 | arc fit swallows a rapid / feed change at the 2nd point | FIXED (post and cam2d) |
| M3 | refitted arcs written as ~180 deg R arcs | FIXED - our refitted arcs split at 120 deg (still R format); Mastercam's stored arcs unchanged (180) |
| M4 | depth passes capped at 200 silently, NaN depth | FIXED - op fails with the count needed; non-finite depths refused |
| M5 | facing box from the first 4096 points | FIXED - all points |
| M6 | chains > 256 / tabs > 16 / unrebuildable chains dropped quietly on take-over | FIXED - the op stays as stored, reason reported |
| M7 | damaged .tooldb loads partially | FIXED - load fails unless every step ends in SQLITE_DONE |
| M8 | unchecked allocations | FIXED - xmalloc / xrealloc / xcalloc (clean exit on out of memory) in the file readers / writers; capacity overflow guarded |
| M9 | saveops reads past short OP_ID records | FIXED - length checks |
| M10 | rpm int overflow in fs_calc | FIXED - range-checked (< 1e6) before conversion |
| M11 | source part protection by string compare | FIXED - same_file (inode / Windows file id) |
| M12 | tool edits leave stored toolpaths for the old tool | FIXED - those ops are marked stale (saved in the project), the post refuses them until regenerated |
| M13 | long project lines split | FIXED - read_line (any length) |
| M14 | post writes no coolant | OPEN - the user's current post (H0 format) writes none; 44 older programs (H<n> format) write M7 / M9. Needs the user's choice; not changed (LinuxCNC rule) |
| L1 | uninitialised error text | FIXED |
| L2 | duplicate ops past 256 edited ops | FIXED - unbounded list |
| L3 | ops lost at 256 | FIXED - limit 1024 everywhere (= selection size), checked |
| L4 | template / config caches never refresh | OPEN (documented): edits to paths.conf or template parts need a restart |
| L5 | rpx ftell -1 | FIXED |
| L6 | XT schema key search past the buffer | FIXED |
| L7 | cmp_seq overflow | FIXED |
| L8 | temp-file name truncation | FIXED - refused if it does not fit |
| L9 | dangling cur_rec | no change (safe as used) |
| L10 | NaN / huge numbers to int | FIXED - such values are ignored (defaults kept) |
| L11 | negative drill cycle posts G81 | FIXED - refused |
| L12 | mcw_open unchecked I/O | FIXED |
Found while fixing: camops_attach (undo / redo / paste), readd_camops, regen_ops_for_tool and project reload all
re-appended our ops at the end of the program - FIXED (positions kept; the project stores a TREE line); undo of an
edit restores the op in place; Clipper exceptions (the user's "Coordinate outside allowed range" abort) are caught and
fail the one operation; every generated move is checked finite.

# apprenticeCAM viewer: robustness / fragile-function review

Scope: `viewer/*.c *.h` (vendor/ ignored). Read-only review; nothing in the repo was changed or run.
The only thing executed was a small harness in this folder (`fmtn_t.c`), a copy of `fmtn()`, used to confirm C3.
"Verified" means the code path was read end to end. Each finding names the read sites.
Line numbers are from 2026-10-15. `mcam_saveops.c` changed on disk during the review; the cited lines were re-checked after the change.

Severity: **CRITICAL** = memory corruption, wrong G-code or data loss. **HIGH** = likely crash or hang on bad input, or silently wrong output that needs an unusual but realistic workflow. **MEDIUM** = fragile, or wrong only at the edges. **LOW** = hygiene.

---

## CRITICAL

### C1. Same-tool operation change skips the op's clearance move, giving a diagonal rapid down to the feed plane
- `nc_post.c:240-243`: when the next op uses the same tool, only `S.. M3` is written. Nothing moves to the tool record's position (`e->p` = `op->m[0]`). `prev` is still set to it at `:258`.
- `nc_post.c:139` (`camop_events`) starts at `k = 1`, so `m[0]` is never written.
- Every generated op starts with `m[0] = (x, y, clearance)` rapid, then `m[1] = (x, y, feed plane)` rapid (`cam2d.c:1059-1061`, `cam2d_ops.c:41,57,109,163,251`, `cam2d.c:1373`).
- Scenario: op 1 (contour, T1) ends at clearance over its last point. Op 2 (pocket, T1) starts across the part. The post writes `G0 X<op2 start> Y.. Z<top+2>`, a single diagonal rapid from clearance down to 2 mm above the stock. It can cross clamps or uncut stock.
- The same pattern is in `cam2d.c:1551-1554` (`cam_write_gcode`, used by cam_cli).
- `prev` is also wrong for an arc that comes first after the same-tool switch (`nc_post.c:280`).
- Verified: `nc_post.c:119-153`, `230-258`; `cam2d.c:1040-1070`.

### C2. After a G99 drill cycle the post still thinks Z is at clearance, so the retract to clearance is dropped
- `nc_post.c:260-271` writes `G99 G81/G83/G73`, which leaves the tool at the R plane. It updates `w.X`, `w.Y`, `w.F` but not `w.Z`.
- `:272-273` (`EV_DRILL_END`) clears only `w.G`.
- The next `EV_RAPID` to `(last x, last y, clearance)` (`nc_post.c:134-135`) compares Z with the old `w.Z`. For a drill op that is the tool-change Z, which is `m[0].z = clearance` (`cam2d.c:1373`). They match, so `words()` writes nothing. The tool stays at the R plane while the post's state says clearance.
- If the next op uses the same tool, its first rapid crosses the part at R-plane height (and C1 adds a diagonal).
- `cam2d.c:1571` (the older writer) does this correctly: it clears `w.x/y/z` and writes `G0 Z<clearance>`. `nc_post.c` lost that.
- Stored Mastercam drill NCI goes through the same branch, so it is affected whenever the Z after the cycle equals the cached Z from before it.
- Verified: `nc_post.c:230-277`, `cam2d.c:1361-1379`, `1557-1574`.

### C3. `fmtn()` stack-buffer overflow, and no finite or range check on coordinates before output
- `nc_post.c:162-169` (and the copy at `cam2d.c:1408-1417`) formats into `char t[48]`, then `strcpy(o, q)`. Every caller's buffer is `char[24]`: `t`, `t1-t3`, `xs/ys/zs/rs/fs/qs`, and `W.X/Y/Z/F` via `strcpy(mods[k], t)` at `:178` and `:255`.
- Harness results (`fmtn_t.c`): 1e22 gives a 25-byte string, -1e22 gives 26, 1e30 gives 33, 1e300 gives 48. That is up to 24 bytes written past the end of a stack buffer. At 1e300 the printed number is also wrong (`snprintf` truncates and the trailing-zero strip eats integer digits).
- Values come straight from the stored NCI with `rd_d` (`nc_post.c:52-72`), and nothing validates them. `mcam_load.c:351` only leaves points above 1e6 out of the extents; it keeps them.
- NaN and Inf print as `Xnan` / `Xinf`. LinuxCNC rejects those, but values like 1e7 or a wrong Z come out as valid G-code.
- Scenario: a corrupted or hostile `.mcam` with a 1e25 coordinate in a move record corrupts the stack during Post.
- Verified: code read, plus the harness.

### C4. Operations silently left out of the posted program, while the message says all were posted
- `nc_post.c:199-207`: an op with no events is skipped. Only "no ops at all" is reported (`:209`). Ways an op ends up with no events:
  - the part file can't be reopened at post time: `fopen` failure is ignored at `:193-195`, `d = NULL`, and every stored op is dropped;
  - a stored op has no NCI section (a dirty op);
  - a generated op has `nm == 0` (`:121`). That happens when a regenerate fails in `regen_ops_for_tool`: `mcamui.c:4051-4052` frees `op->m` and sets `nm = 0`, keeps the old polylines on screen, and reports only through `cam_msg`.
- `mcamui.c:3777` then reports `posted %d operation(s)` using `F.nops`, not the number actually posted.
- Scenario: the user edits T3's diameter, the pocket using T3 fails to regenerate (for example the tool no longer fits), and the posted program has no pocket while the screen still shows it and the status says every op was posted.
- Verified.

### C5. Saved chains point to entities by array index, so a reloaded project can machine the wrong geometry
- `cam_write_ops` writes `CHAIN ... <ent index>:<rev>` (`cam2d.c:1640-1643`). `cam_load_ops` drops only out-of-range indices (`cam2d.c:1700-1701`).
- The project stores the PART path but no hash, mtime or entity count. If the part is re-saved in Mastercam, the indices point to other entities.
- Worse, it happens even when the part is unchanged:
  - `cam_solid_chain_ents` (`cam_fileop.c:46-50`) appends Solid-Chain edges to `F.ents` with `created = 0`, so `doc_write` doesn't save them (`mcamui.c:1321-1325`);
  - `doc_read` re-applies `GEO` lines before `cam_load_ops` (`mcamui.c:1366-1397`).
  - So a line drawn after a solid-chain op was loaded was at index N+k when saved and comes back at index N. Every chain that used it now points at a solid edge or another drawn entity.
- The ops regenerate without any warning (`n < nop_lines` catches only ops that fail outright).
- Verified.

### C6. Deleting geometry used by an op leaves dangling chains: deleted lines turn into a point at (0,0)
- `ent_off` and `delete_selected_ents` (`mcamui.c:2154-2158`, `2265-2275`) set `type = 0` with no check against op chains. Tools have such a check (`:3824-3828`); entities don't.
- `cam_chain_polyline` (`cam2d.c:486-501`) treats anything that is not `E_LINE` as an arc. A deleted line has `c = 0` and `r = 0`, so its sample is the point (0,0). A deleted arc is still machined.
- The next regenerate (tool edit `regen_ops_for_tool`, parameter edit, reopening the project) builds a toolpath with a spike to the origin.
- `mcam_saveops.c:400-411` also writes chain references to entity ids whose records it drops at `:447`.
- Verified.

### C7. `mcam_saveops` writes past a heap block when it reuses an edited op's own record
- The template found by `file_op_id` (`mcam_saveops.c:241`) has no length check. Templates found by `find_op` must be exactly 0x2938 bytes (`:104`).
- The copy `r = malloc(seg.len)` (`:255`) is then written at fixed offsets up to `e+0x1588`: `:284-329`, for example `wd(e+0x1580)` and `e[0x1567]`.
- `file_op_id` comes from the project or sidecar `P file_op_id` and is not tied to the record size. A project paired with a part where that op id now names a short OP_ID record (anything ≥ 0x30 bytes is classed as `sub == 1`, `mcam_write.c:45`) overflows the heap.
- The same applies to the header record:
  - `H = m.d + seg[hdr].off + 4` is written at `H+0x6c..0x7f` (`:450-464`, and `new_base` `:150-153`);
  - a sub-0x0d record only has to be ≥ 0x6a bytes (`mcam_write.c:45`), so a short header lets these writes run past it.
- Precondition: a mismatched or malformed file. Result: heap corruption during Save as MCAM.
- Verified.

### C8. Copying a library tool into the part ignores units
- `part_add_tool` (`mcamui.c:3813-3820`) and "Copy to part" (`:4031-4033`) copy the `Tool` verbatim. `Tool.is_metric` and `ToolLib.metric` are never compared with `F.metric`.
- Scenario: a 1/2" endmill from an inch `.tooldb` copied into a metric part becomes D0.5 mm with feeds in in/min read as mm/min. Contours are offset by 0.25 mm instead of 6.35 mm, so the cutter centre runs about 6 mm inside the intended wall.
- Verified: grep finds no unit handling in mcamui.c besides `tool_new(..., F.metric != 0)`.

### C9. Solid Chain geometry is always in mm, so inch parts get a toolpath 25.4 times too large
- `xt_top_loop` (`xt_mesh.c:478-500`) multiplies all coordinates by 1000 (metres to mm). `cam_solid_chain_ents` (`cam_fileop.c:38-57`) uses them as part coordinates.
- `scene.c:281-291` picks 1000 or 1000/25.4 for display by matching extents, but the CAM path does not.
- `xt_cylinders` (kind-27 holes, `cam_fileop.c:277-281`) has the same ×1000. In an inch part the XY match at 1e-3 fails, so d = 0 and those holes are skipped by thread mill.
- Scenario: an inch part with a "Solid Chain" contour is regenerated or edited, and the toolpath is 25.4 times the size of the part.
- Verified.

---

## HIGH

### H1. Editing or regenerating an op moves it to the end of the program
- `mcam_add_op` appends (`mcam_load.c:379-397`). The post walks `F.ops` in array order (`nc_post.c:199`).
- Every edit path removes the op and re-adds it:
  - editing a stored op: `mcamui.c:3752-3753`;
  - changing a tool: `regen_ops_for_tool`, `:4053-4055`;
  - `readd_camops` (`:4115-4122`), which moves all generated ops after all stored ops;
  - Edit Common Parameters: `:4249`, `:5287`.
- Meanwhile `mcam_saveops.c:264` deliberately keeps the edited op's place in the saved part. So the in-app post and the saved .mcam disagree on machining order.
- Scenario: Face, Rough, Drill, Finish. The user edits Face's depth and the post becomes Rough, Drill, Finish, Face.
- Verified.

### H2. The post re-reads the part file and assumes it hasn't changed since loading
- `nc_post.c:192-195` opens `f->path` again and uses `f->nci_off` and the op ids from load time. If Mastercam re-saved the part since, ids and offsets belong to another file version, and mismatched toolpaths are posted with no warning.
- Also at `:194`: `fseek`/`ftell` unchecked (`ftell = -1` gives `len = SIZE_MAX`); `malloc` unchecked, so a NULL pointer goes to `fread`.
- Verified.

### H3. Spindle speed and feed reach the G-code unchecked
- `nc_post.c:242,249` write `S%d` with `abs(rpm)` and pick M4 when rpm < 0. S0 is written as is; `abs(INT_MIN)` is UB.
- Feeds are written with `fmtn` (`:180,262`): F0, negative and NaN all pass.
- Sources:
  - stored NCI `rd_i(buf,0x38)` (`:52`);
  - `.tools` text, where `tool_parse` accepts any number (`toollib.c:251`);
  - the sidecar `P rpm`;
  - `cam_common_apply` (`cam_fileop.c:378-380`);
  - `fs_calc` (H17).
- Scenario: a corrupted or hand-edited tool gives `S0 M3` with cutting moves (LinuxCNC has no spindle-at-speed interlock by default), or a negative rpm gives M4, a reverse spindle.
- Verified: there is no check anywhere in `nc_post.c`.

### H4. Unknown units are posted as metric without a word
- `nc_post.c:189`: `inch = f->metric == 0`, so `metric == -1` (a new part, or a part with no 1016 NCI records, e.g. every op dirty) writes G21.
- `cam2d.c:1530` at least writes "mm - part units unknown, assumed". `nc_post` writes nothing, and `post_ops` doesn't ask.
- Verified.

### H5. XT parser reads past the buffer on header lengths
- `xt_mesh.c:122-128`: `q[1]` is read when the newline is the last byte, and `n1`/`n2` from the file are added to `o` with no `o+4 <= len` check before the next `memcpy`. A corrupt solid in a part makes `load_file` → `mesh_solids` read arbitrary memory (crash).
- `:139-140`: `count`/`idx` are read with only `o+2 <= len` checked.
- `:149`: `4 * usfld` is computed in 32 bits.
- Verified.

### H6. XT loop walks can run forever, hanging part open
- `xt_top_loop` `xt_mesh.c:476` follows `LOOP.next` with no guard.
- `xt_mesh` `:444` stops only once `nr` reaches 64, and `nr` doesn't increase when `loop_ring` fails. A cyclic `next` chain whose loops have no halfedge loops forever.
- `xt_mesh` runs during `load_file`.
- Verified.

### H7. A failed post deletes the previous NC program
- `post_ops` (`mcamui.c:3767-3776`) opens `<part>-mcamui.nc` with `"w"`, which truncates the last good program. On a post error (e.g. "mixed units", "Tap cycle") it calls `remove(out)`. On `fclose` failure it leaves a truncated file under the real name.
- Unlike `doc_write`, there is no tmp-file-and-rename.
- Verified.

### H8. Windows: saving over an existing file always fails
- `rename(tmp, dst)` fails on Windows when `dst` exists. That hits `mcam_saveops.c:472` (re-export to the same name), `toollib.c:228` (saving a library over itself) and `mcam_write.c:117`.
- `doc_write` (`mcamui.c:1335`) works around it with `.bak`; the others don't. `platform.h` has no `MoveFileEx` wrapper.
- It fails safely (error, tmp removed), so no data is lost, but the feature doesn't work on the Windows build (`win/mcamui.exe`).
- Verified.

---

## MEDIUM

- **M1. NCI tool-name and material strings may not be terminated.** `stored_events` copies `min(sz, 0x370)` bytes into `buf[0x370]` (`nc_post.c:39-40`). A record with `sz >= 0x370` (allowed up to 0x6e0) has no NUL, and `snprintf("%s", buf+0x18)` (`:48,50`) reads past the stack buffer. Verified.
- **M2. Arc fitting never checks the second point.** `fit_arc` (`nc_post.c:95-96`, `cam2d.c:1450-1451`) starts at `k = i0+2`, so `m[i0+1].rapid` and `m[i0+1].feed` are never looked at. A rapid or a feed change at the second point is swallowed into a G2/G3 at `m[i0].feed`. Verified.
- **M3. r180 style writes 180° pieces in R format.** `nc_post.c:300-301,316`. `cam2d.c:1500-1502` says R arcs near 180° are ill-conditioned (a 0.001 change in R moves the centre by about √(2R·0.001)) and splits at 120°. For fitted arcs on generated ops the endpoints sit within tol of the circle, so a half-circle piece can shift its centre or trip LinuxCNC's radius check. Matches Mastercam's post for stored NCI; fragile for generated moves. Verified.
- **M4. Depth passes are capped at 200 without notice.** `zlevels` (`cam2d.c:518-524`) clamps to `max = 200`, so a depth of 50 at 0.2 stepdown cuts 0.25 per pass. NaN depth makes `(int)ceil(NaN)` UB. Verified.
- **M5. Facing area built from the first 4096 points only.** `gen_face` (`cam2d_ops.c:13-14`) fills its bounding box from at most 4096 chain points. A finely sampled chain gets a box that's too small, so part of the face is left uncut. Verified.
- **M6. Chains and tabs over the limits are dropped quietly.** `cam_op_from_file` (`cam_fileop.c:253,260,266`) drops chains past 256 and tabs past 16 without telling the user. Verified.
- **M7. Partial tool library loads without an error.** `load_tooldb` (`toollib.c:138,169`) stops at the first non-ROW result (`SQLITE_CORRUPT` / `BUSY`) and returns success with whatever it read. Verified.
- **M8. Allocations aren't checked.** `realloc`/`malloc` NULL is never checked in `mcam_load.c:37,41,51,82,145,168,327,330`, `nc_post.c:26,194`, `mcam_write.c:16,25`, `mcam_saveops.c:60,255,338`, `toollib.c:91`, `rpx.c:75`, `xt_mesh.c:130`. Huge or hostile counts end in a NULL dereference. Also `int` capacity doubling at `mcam_load.c:37`. Verified.
- **M9. Saveops reads past short OP_ID records.** `mcam_saveops.c:183-189` reads `e+0xe6`, and `:203-204` reads `e+0x2f4` and `e+0x5bc`, for any `sub == 1` record without a length check. That reads past the record, and past the file buffer if it is the last record. Verified.
- **M10. Feed/speed calculation has no rpm range check.** `fs_calc` (`feedspeed.c:187`) computes `(int)(sfm*3.82/D)`. sfm comes from the material file and `D >= 1e-9` is the only check, so the float-to-int conversion can overflow (UB). `r.ok` only checks `rpm != 0` and finite feeds. Verified.
- **M11. Source-part protection compares strings.** `mcam_save_part` (`mcam_saveops.c:175`) uses `strcmp(src, dst)`, so `./x.mcam`, a symlink, or a different case on Windows replaces the user's original Mastercam part. Verified.
- **M12. Tool edits don't affect stored toolpaths.** Changing the diameter or number of a tool used by stored Mastercam ops (`mcamui.c:3929-3936`) regenerates only generated ops. Stored NCI computed for the old tool is still posted, with no warning. Deleting such a tool is blocked (`:3827`); editing it is not. Verified.
- **M13. Long project lines are split.** The project/sidecar line buffer is 65536 bytes (`cam2d.c:1662`, `mcamui.c:1345`). `cam_write_ops` writes a whole chain on one line, so a chain of more than about 9000 entities is split by `fgets` and the rest is ignored. The chain comes back truncated with no warning. Verified.
- **M14. `nc_post` never writes coolant.** No M8/M9 although the parameters dialog offers "Flood: On" (`mcamui.c:5735`) and `cam_write_gcode` writes it. Verified.

## LOW

- **L1. Uninitialised error text.** `tool_new` prints `err` (`toollib.c:46`) when `site_file` returns "" and `load_tooldb` was never called.
- **L2. Duplicate ops after saving many edited ops.** `dropped_ids[256]` (`mcam_saveops.c:431-432`): past 256 edited ops, the originals stay in the saved part next to the edited copies.
- **L3. Ops lost at the 256-op limit.** At `ncamops == 256` an op is still added to `F.ops` but not tracked (`mcamui.c:3754`), or the op is freed after its stored original was removed (`:5288`). `doc_read` (`:1399`) drops past 256 without saying so.
- **L4. Template caches never refresh.** `mcam_saveops.c:233-234` loads `lib[24]` once, excluding the first save's `src` only. `cam_fileop.c:313` `df[2]`, `toollib.c:45` `D`, the `rpx.c:37` cache and the per-TU `site_path`/`config_dir` statics (`paths.h:13-36`, `platform.h:96-113`) never refresh. Edits to `paths.conf` or template parts need a restart.
- **L5. rpx file size.** `rpx.c:74-75`: `ftell == -1` leads to `malloc(0)` and then a write to `d[0]`.
- **L6. XT schema key search.** `xt_schema_for` (`xt_mesh.c:507`) runs `memchr(h, ';', 64)` without checking that 64 bytes remain.
- **L7. Op sort overflow.** `cmp_seq` (`mcam_load.c:245`) subtracts ids (`x->id - y->id`), which can overflow and give the sort an inconsistent comparator.
- **L8. Temp-file name truncated.** `tmp[1100]` holds `"%s.tmp"` (`mcam_write.c:108`, `mcam_saveops.c:470`, `toollib.c:220`). A path of 1095 characters or more is truncated, so the temp file goes to a different name (with 1099 or more, a prefix of the destination path).
- **L9. Dangling record pointer.** `cur_rec` (`mcam_load.c:80,85`) still points into the freed file buffer after `mcam_load`. It's safe only because `mcam_new_ent` resets it.
- **L10. Numeric conversions.** `tool_parse` and the sidecar `P` lines (`toollib.c:251`, `cam2d.c:1676`) convert to `int` through `atof`/`(int)v` (UB for huge or NaN). `tool_parse` accepts NaN feeds. RP `off` is cast to `uint16_t`.
- **L11. Negative drill cycle.** `nc_post.c:210` rejects only `cyc > 2`, so a negative cycle from a file posts as G81.
- **L12. `mcw_open` I/O unchecked.** `mcam_write.c:23-26`: `fseek`/`ftell`/`malloc` unchecked. On `mcw_open_mem` failure `mcam_template_parts` (`mcam_saveops.c:92`) leaks `d`/`seg`.

---

## Checked and found robust (no finding)

- `mcam_load` record framing: all `entity()` field reads stay inside `n`; `cstr` limits stay inside the record.
- `matlib_load` and `cam_fileop` reads are bounds-checked (`rdd/rd16/rd32` take `n`).
- `ipack_find` bounds checks are correct.
- The `CP` macro is used only on arrays (every call site checked).
- `doc_write` is atomic and fsyncs.
- Config parsers (kmp, mtb, Workspace, config) use bounded `sscanf`/`snprintf`.
- No threads anywhere.
