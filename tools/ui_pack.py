#!/usr/bin/env python3
"""Pack the extracted ribbon/manager *structure* (names, layout, command bindings) for the app (mcamui at the time; removed 2026-10-22).
No original assets are packed: icon file names and Mastercam description texts are left empty.

Original: Pack the extracted Mastercam UI (ui_ribbon.json, ui_opmanres.json) into a flat tab-separated file for mcamui.

Usage: ui_pack.py RIBBON.json OPMANRES.json OUT.txt
Lines (fields separated by TAB; tabs/newlines inside text are replaced by spaces):
  TAB      name  context(empty for main tabs)  keys
  GROUP    name
  BTN      kind  id  text  tooltip  description  icon_small  icon_large  ft_function  dll  entry   (top level in group)
  SUB      same fields as BTN, child of the previous BTN
  GAL      text  description  columns  icon_w  icon_h  (gallery; items follow)
  GITEM    name  description  icon
  FILE     text  description  icon_large                        (File tab / backstage commands)
  TBAR     toolbar_id  button_id  tip  icon                     (Toolpaths manager toolbars 800, 804)
  MENU     menu_id  depth  text                                 (classic menu bar, menu 135)
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from debrand import debrand

def c(v):
    return '' if v is None else str(v).replace('\t', ' ').replace('\r', ' ').replace('\n', ' ').strip()

def btn(tag, e):
    ft = e.get('ft') or {}
    # icon columns carry only a size hint ('L' = shown large, 'S' = small); no image references, no descriptions
    return '\t'.join([tag, c(e.get('kind')), c(e.get('id')), c(e.get('text')), c(e.get('tooltip')), '',
                      'S' if e.get('icon_small') else '', 'L' if e.get('icon_large') else '',
                      c(ft.get('function')), c(ft.get('dll')), c(ft.get('entry'))])

def main():
    rib = json.load(open(sys.argv[1])); opm = json.load(open(sys.argv[2]))
    out = []
    for e in rib['file_tab'].get('elements', []) + [x for g in rib['file_tab'].get('groups', []) for x in g['elements']]:
        out.append('\t'.join(['FILE', c(e.get('text')), '', '']))
    for t in rib['tabs']:
        out.append('\t'.join(['TAB', c(t['name']), c(t.get('context')), c(t.get('keys'))]))
        for g in t['groups']:
            out.append('\t'.join(['GROUP', c(g['name'])]))
            for e in g['elements']:
                if e.get('kind') == 'Button_Palette':
                    gl = e['gallery']
                    out.append('\t'.join(['GAL', c(e.get('text')), '', str(gl['columns']),
                                          str(gl['icon_size'][0]), str(gl['icon_size'][1])]))
                    for it in gl['items']:
                        out.append('\t'.join(['GITEM', c(it['name']), '', '']))
                    continue
                out.append(btn('BTN', e))
                for ch in e.get('children', []):
                    out.append(btn('SUB', ch))
    for tid in ('800', '804'):
        for b in opm['toolbars'].get(tid, {}).get('buttons', []):
            if b['id']:
                out.append('\t'.join(['TBAR', tid, str(b['id']), c((b.get('tip') or '').split('\n')[-1]), '']))

    def menu(items, depth):
        for it in items:
            if it.get('separator'):
                out.append('\t'.join(['MENU', '135', str(depth), '-']))
            else:
                out.append('\t'.join(['MENU', '135', str(depth), c(it['text'])]))
                if 'popup' in it:
                    menu(it['popup'], depth + 1)
    menu(opm['menus'].get('135', []), 0)
    kept = []                                     # product name out (tools/debrand.py): rename or drop the entry
    for line in out:
        f = line.split('\t'); g = [f[0]] + [debrand(x) for x in f[1:]]
        if None not in g: kept.append('\t'.join(g))
    out = kept
    open(sys.argv[3], 'w').write('\n'.join(out) + '\n')
    print(f'{len(out)} lines -> {sys.argv[3]}')

if __name__ == '__main__':
    main()
