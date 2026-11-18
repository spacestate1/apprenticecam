# Overnight plan 2026-10-15 22:30 -> 2026-10-16 06:00 (autonomous, no user input)

Rules (unchanged): no improvisation (cite decompiled code / data, else NOT DECODED); memory: <= 3 agents at once, one
Ghidra process machine-wide, ulimit -v 4000000; regressions after every code change: fileop 0/189, post 0/16; no
writes to the user's Mastercam config / part files; no DISPLAY :1. Screenshots -> screenshots/2026-10-15_*.png.

Queue (in order; each item = decode if needed, implement, test, regressions, tracker update):
1. Extrude 1:1 (formats/extrude.md, 32 differences) - running.
2. Backplot leftovers (formats/backplot.md section 13): Options dialog 1228, motion colours, endpoints, tool
   appearance / spin, rapid time, Details read-only, tree Path level.
3. Wireframe live-entity pattern for the other commands that use it (fillet, trim, ... per formats/wireframe.md),
   plus selection highlight review.
4. Reverse toolpath (P9, formats/reverse_toolpath.md: spec done, button greyed) - implement.
5. mcce Dynamic engine port (P1/P2/P3/P8) from formats/mcce_engines.md incl. section 6 - replace our own pattern.
6. Area Mill decode (mcmill CHST2DWorkItemStd) and port.
7. Tool Manager remaining: Save tools to library / Compress / copy-paste / Import decode + implement; holder wizard.
8. Remaining NOT DECODED items from formats/provenance_audit.md, in order of user visibility.
Status is appended below as items finish.

## Status log
- 22:49 item 2 Backplot section 13 DONE (Options 1228, colours, endpoints, tool appearance, Info/Details/Path). Open: rapid time needs machine axis rapid rates (decode queued under 8), wire dialog 1229, quick verify. Regressions 0/189, 0/16.
- 22:50 item 3 Reverse toolpath DONE (viewer/nci_reverse.c; 174/189 ops reverse to the exact original moves, 15 differ as the decode says; reverse twice not identical, as Mastercam). Regressions 0/189, 0/16.
- 22:56 item 1 Extrude DONE 25/32, partly 6 (target search, auto op type, chain highlight, trim to faces (no faces), plane orientation non-Z = kernel limit), not done: drag arrows, editing an existing extrude. Regressions 0/189, 0/16.
- 23:07 item 4 wireframe live pattern DONE for 25/42 commands (formats/wireframe.md 'Live results'); Chamfer Esc now commits as decoded. Regressions 0/189, 0/16.
- 23:14 item 6 Area Mill: parameters / plan / region preprocessing decoded and ported (viewer/area_mill.c, formats/area_mill.md); the pass pattern lives in McHmm.dll -> import + decode started (user approved all). Regressions 0/189, 0/16.
- 23:59 item 5 mcce Dynamic Mill port DONE (viewer/mcce_dyn.c default for Dynamic Mill; median deviation 0.51 -> 0.04 mm, entries 32/35 within 0.1 mm); Dynamic Contour / Peel partial (no reference ops). Baseline refreshed for 35 dynamic ops (old: regress/base_before_mcce_port_2026-10-16). item 7 Tool Manager remaining DONE (copy/paste, save-to, compress, new/browse/import, keys, icon views, resize, settings). Post 0/16.
- 00:01 item 6b McHmm imported (ghidra_hmm); Area Clearance stepover rule decoded + ported (oracle p95 2.67 -> 0.93 mm); baseline refreshed for buckle-v1x_2. Placeholder still OURS: straight feed links between passes (to replace with the decoded linker). Core Roughing not decoded. Post 0/16.
- 07:02 run ended. Usage limit stopped all agents ~02:00-03:00 (reset 03:20); not resumed before 06:00. Paused, work saved: McHmm linker / ramp Z / Core Roughing, machine-record rapid time, mcce follow-up (stay-inside entries, comparators, guided planner).
