# The dialog templates' font (user decision UI-4, formats/user_decisions.md)

`tahoma.ttf` is **Wine's Tahoma** (wine 11.22, `/usr/share/wine/fonts/tahoma.ttf`; source: the Wine project, `fonts/tahoma.sfd`,
https://gitlab.winehq.org/wine/wine). Copyright (c) 2004 Larry Snyder, based on Bitstream Vera Sans (c) 2003 Bitstream, Inc.;
licensed under the **GNU Lesser General Public License 2.1 or later** (`LGPL-2.1-or-later.txt`). It is a separate work
shipped unmodified next to the program and loaded at run time (QFontDatabase::addApplicationFont); the LGPL places no
condition on the MIT-licensed code. It is not Microsoft's Tahoma: Wine made it as a metric-compatible replacement.

Why this font: Mastercam's dialog templates (its resource DLLs) ask for MS Sans Serif 8 (1342 dialogs) or MS Shell Dlg 8
with DS_SHELLFONT, i.e. Tahoma 8 (230 dialogs). Both give the 6 x 13 px dialog base units the layouts are drawn in
(viewer-qt/src/chaindlg.cpp chainDu). Widths at 8 pt / 96 dpi (11 px), over the 3856 static / button / combo texts of the
mccoreres, mcmillres, mcmsurfres and mclatheres templates, against MS Sans Serif 8 (the FNT advance table of Wine's
`sserife.fon`, its metric clone):

| font (11 px unless noted) | a-Z sum (52 chars) | total width | per-text mean | 95th pct | strings > 2 px wider |
|---|---|---|---|---|---|
| MS Sans Serif 8 (reference) | 323 | 100 % | 100 % | 100 % | 0 |
| Wine Tahoma | 313 | 103.1 % | 103.0 % | 107.6 % | 1614 |
| Liberation Sans | 333 | 103.9 % | 104.0 % | 107.5 % | 2052 |
| Nimbus Sans | 331 | 103.6 % | 103.7 % | 107.1 % | 1877 |
| Noto Sans (the app's UI font, used before) | 333 | 109.9 % | 109.9 % | 115.9 % | 3102 |
| DejaVu Sans | 353 | 117.4 % | 117.3 % | 122.2 % | 3643 |

Tahoma 8 is itself ~3 % wider than MS Sans Serif 8 on Windows; Wine's Tahoma has Tahoma's advance widths (its 52-letter sum
313 gives the documented 6 x 13 base units). Measured in the app (tools/tests/ui_text_check.sh, every page of every op
dialog, the text fit fixes on): Noto Sans 11 px 84 problems, Liberation Sans 66, Wine Tahoma 63 (before the other UI-2..5
changes). Microsoft's own Tahoma / Microsoft Sans Serif could not be read on this machine (WOF-compressed system files), so the
widths above are Wine's metric clones'.
